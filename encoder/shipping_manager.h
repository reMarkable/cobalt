// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_SHIPPING_MANAGER_H_
#define COBALT_ENCODER_SHIPPING_MANAGER_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "./logging.h"
#include "encoder/envelope_maker.h"
#include "encoder/send_retryer.h"
#include "encoder/shuffler_client.h"
#include "third_party/clearcut/uploader.h"

namespace cobalt {
namespace encoder {

using send_retryer::SendRetryerInterface;

// ShippingManager is a central coordinator for collecting encoded Observations
// and sending them to the Shuffler. Observations are accumulated in memory
// and periodically sent in batches to the Shuffler by a background worker
// thread on a regular schedule. ShippingManager also performs expedited
// off-schedule sends when too much unsent Observation data has accumulated.
// A client may also explicitly request an expedited send.
//
// ShippingManager uses a SendRetryer to send Observations to the Shuffler so
// in case a send fails it will be retried multiple times with exponential
// back-off.
//
// ShippingManager uses gRPC to send to the Shuffler. The unit of data sent
// in a single gRPC request is the *Envelope*. ShippingManager will distribute
// the collection of Observations it controls into one or more Envelopes
// in order to try to achieve an efficient size Envelope to send.
//
// Usage: Construct a ShippingManager, invoke Start() once, and repeatedly
// invoke AddObservation(). After Start() has been invoked calls to
// AddObservation() are thread safe: It may be invoked concurrently
// by multiple threads. Optionally invoke RequestSendSoon() to expedite a send
// operation.
//
// Usually a single ShippingManager will be constructed for an entire
// client device and all applications running on that device that wish to use
// Cobalt to collect metrics will make use of this single instance.
class ShippingManager {
 public:
  // Parameters passed to the ShippingManager constructor that control its
  // behavior with respect to the size of the data stored in memory and
  // sent using gRPC.
  class SizeParams {
   public:
    // max_bytes_per_observation: AddObservation() will return
    // kObservationTooBig if the given Observation's serialized size is bigger
    // than this.

    // max_bytes_per_envelope: When collecting Observations into Envelopes,
    // ShippingManager will not build an Envelope larger than this size. Since
    // ShippingManager sends a single Envelope in a gRPC request, this value
    // should be used to ensure that ShippingManager does not exceed the
    // maximum gRPC message size.
    //
    // max_bytes_total: ShippingManager will perform an expedited send if the
    // size of the accumulated, unsent Observation data exceeds 60% of this
    // value. If the size of the accumulated, unsent Observation data reaches
    // this value then ShippingManager will not accept any more Observations:
    // AddObservation() will return kFull, until ShippingManager is able to send
    // the accumulated Observations to the Shuffler.
    //
    // min_envelope_send_size: ShippingManager will attempt to combine Envelopes
    // with sizes smaller than this value (in bytes) into Envelopes whose size
    // exceeds this value prior to sending to the Shuffler.
    //
    // REQUIRED:
    // 0 <= max_bytes_per_observation <= max_bytes_per_envelope <=
    // max_bytes_total
    // 0 <= min_envelope_send_size <= max_bytes_per_envelope
    SizeParams(size_t max_bytes_per_observation, size_t max_bytes_per_envelope,
               size_t max_bytes_total, size_t min_envelope_send_size)
        : max_bytes_per_observation_(max_bytes_per_observation),
          max_bytes_per_envelope_(max_bytes_per_envelope),
          max_bytes_total_(max_bytes_total),
          min_envelope_send_size_(min_envelope_send_size) {
      CHECK_LE(max_bytes_per_observation_, max_bytes_per_envelope_);
      CHECK_LE(max_bytes_per_envelope_, max_bytes_total_);
      CHECK_LE(min_envelope_send_size_, max_bytes_per_envelope_);
    }

   private:
    friend class ShippingManager;
    size_t max_bytes_per_observation_;
    size_t max_bytes_per_envelope_;
    size_t max_bytes_total_;
    size_t min_envelope_send_size_;
  };

  // Use this constant instead of std::chrono::seconds::max() in
  // ScheduleParams below in order to effectively set the wait time to
  // infinity.
  static const std::chrono::seconds kMaxSeconds;

  // Parameters passed to the ShippingManager constructor that control its
  // behavior with respect to scheduling.
  //
  // schedule_interval: How frequently should ShippingManager perform regular
  // periodic sends to the Shuffler? Set to kMaxSeconds to effectively
  // disable periodic sends.
  //
  // min_interval: Because of expedited sends, ShippingManager may sometimes
  // send to the Shuffler more frequently than |schedule_interval|. This
  // parameter is a safety setting. ShippingManager will never perform two
  // sends within a single period of |min_interval| seconds.
  //
  // REQUIRED:
  // 0 <= min_interval <= schedule_interval <= kMaxSeconds
  class ScheduleParams {
   public:
    ScheduleParams(std::chrono::seconds schedule_interval,
                   std::chrono::seconds min_interval)
        : schedule_interval_(schedule_interval), min_interval_(min_interval) {
      CHECK_GE(min_interval.count(), 0);
      CHECK_LE(min_interval_.count(), schedule_interval_.count());
      CHECK_LE(schedule_interval.count(), kMaxSeconds.count());
    }

   private:
    friend class ShippingManager;
    std::chrono::seconds schedule_interval_;
    std::chrono::seconds min_interval_;
  };

  // Parameters passed to the ShippingManager constructor that will be used
  // to construct EnvelopeMakers. See the documentation of the
  // EnvelopeMaker constructor.
  class EnvelopeMakerParams {
   public:
    EnvelopeMakerParams(std::string analyzer_public_key_pem,
                        EncryptedMessage::EncryptionScheme analyzer_scheme,
                        std::string shuffler_public_key_pem,
                        EncryptedMessage::EncryptionScheme shuffler_scheme)
        : analyzer_public_key_pem_(analyzer_public_key_pem),
          analyzer_scheme_(analyzer_scheme),
          shuffler_public_key_pem_(shuffler_public_key_pem),
          shuffler_scheme_(shuffler_scheme) {}

   private:
    friend class ShippingManager;
    std::string analyzer_public_key_pem_;
    EncryptedMessage::EncryptionScheme analyzer_scheme_;
    std::string shuffler_public_key_pem_;
    EncryptedMessage::EncryptionScheme shuffler_scheme_;
  };

  // Parameters passed to the ShippingManager constructor that will be passed
  // to the method SendRetryer::SendToShuffler(). See the documentation of
  // that method.
  class SendRetryerParams {
   public:
    SendRetryerParams(std::chrono::seconds initial_rpc_deadline,
                      std::chrono::seconds deadline_per_send_attempt)
        : initial_rpc_deadline_(initial_rpc_deadline),
          deadline_per_send_attempt_(deadline_per_send_attempt) {}

   private:
    friend class ShippingManager;
    friend class LegacyShippingManager;
    friend class ClearcutV1ShippingManager;
    std::chrono::seconds initial_rpc_deadline_;
    std::chrono::seconds deadline_per_send_attempt_;
  };

  // Constructor
  //
  // size_params: These control the ShippingManager's behavior with respect to
  // the size of the data stored in memory and sent using gRPC.
  //
  // scheduling_params: These control the ShippingManager's behavior with
  // respect to scheduling sends.
  //
  // envelope_maker_params: Used when the ShippingManager needs to construct
  // and EnvelopeMaker.
  //
  // send_retryer_params: Used when the ShippingManager needs to invoke
  // SendRetryer::SendToShuffler().
  //
  // send_retryer: The instance of |SendRetryerInterface| encapsulated by
  // this ShippingManager. ShippingManager does not take ownership of
  // send_retryer which must outlive ShippingManager.
  ShippingManager(const SizeParams& size_params,
                  const ScheduleParams& scheduling_params,
                  const EnvelopeMakerParams& envelope_maker_params,
                  const SendRetryerParams send_retryer_params,
                  SendRetryerInterface* send_retryer);

  // The destructor will stop the worker thread and wait for it to stop
  // before exiting.
  virtual ~ShippingManager();

  // Starts the worker thread. Destruct this object to stop the worker thread.
  // This method must be invoked exactly once.
  void Start();

  // The status of an AddObservation() call.
  enum Status {
    // AddObservation() succeeded.
    kOk = 0,

    // The Observation was not added because it is too big.
    kObservationTooBig,

    // The Observation was not added to the Envelope because the Shipping
    // manager has too large of a backlog of Observations that have not yet
    // been sent. In the current version we use a pure in-memory cache not
    // backed by a persistent cache and so we return this error if the in-memory
    // backlog gets too large. In later versions we will have a persistent
    // cache and so the threshold for returning this error will be much higher.
    kFull,

    // The ShippingManager is shutting down. No more Observations will be
    // accepted.
    kShutDown,

    // The Observation was not added to the Envelope because the encryption
    // failed. This should never happen.
    kEncryptionFailed
  };

  // Add |observation| and its associated |metadata| to the collection of
  // Observations controlled by this ShippingManager. Eventually the
  // ShippingManager's worker thread will use the |SendRetryer| to send
  // all of the accumulated, unsent Observations.
  Status AddObservation(const Observation& observation,
                        std::unique_ptr<ObservationMetadata> metadata);

  // Register a request with the ShippingManager for an expedited send.
  // The ShippingManager's worker thread will use the |SendRetryer| to send
  // all of the accumulated, unsent Observations as soon as possible but not
  // sooner than |min_interval| seconds after the previous send operation
  // has completed.
  void RequestSendSoon();

  using SendCallback = std::function<void(bool)>;

  // A version of RequestSendSoon() that provides feedback about the send.
  // |send_callback| will be invoked with the result of the requested send
  // attempt. More precisely, send_callback will be invoked after the
  // ShippingManager has attempted to send all of the Observations that were
  // added prior to the invocation of RequestSendSoon(). It will be invoked
  // with true if all such Observations were succesfully sent. It will be
  // invoked with false if some Observations were not able to be sent, but
  // the status of any particular Observation may not be determined. This
  // is useful mainly in tests.
  void RequestSendSoon(SendCallback send_callback);

  // Blocks for |max_wait| seconds or until the worker thread has
  // successfully sent all previously added Observations and is idle, waiting
  // for more Observations to be added. This method is most useful if it
  // can be arranged that there are no concurrent invocations of
  // AddObservation() (for example in a test) because such concurrent
  // invocations may cause the idle state to never be entered.
  void WaitUntilIdle(std::chrono::seconds max_wait);

  // Blocks for |max_wait| seconds or until the worker thread is in the state
  // where there are Observations to be sent but it is waiting for the
  // next scheduled send time. This method is most useful if it can be
  // arranged that there are no concurrent invocations of RequestSendSoon()
  // (for example in a test) because such concurrent invocations might cause
  // that state to never be entered.
  void WaitUntilWorkerWaiting(std::chrono::seconds max_wait);

  // Returns the active EnvelopeMaker via move leaving the active EnvelopeMaker
  // empty. This method is most likely only useful in a test.
  std::unique_ptr<EnvelopeMaker> TakeActiveEnvelopeMaker();

  // These diagnostic stats are mostly useful in a testing environment but
  // may possibly prove useful in production also.
  size_t num_send_attempts();
  size_t num_failed_attempts();
  grpc::Status last_send_status();

 private:
  // Has the ShippingManager been shut down?
  bool shut_down();

  // Causes the ShippingManager to shut down. Any active sends by the
  // SendRetryer will be canceled. All condition variables will be notified
  // in order to wake up any waiting threads. The worker thread will exit as
  // soon as it can.
  void ShutDown();

  // The main method run by the worker thread. Executes a loop that
  // exits when ShutDown() is invoked.
  void Run();

  // Helper method used by Run(). Does not assume mutex_ lock is held.
  void SendAllEnvelopes();

  // Helper method used by Run(). Does not assume mutex_ lock is held.
  void SendOneEnvelope(
      std::deque<std::unique_ptr<EnvelopeMaker>>* envelopes_that_failed);

  virtual void SendEnvelopeToBackend(
      std::unique_ptr<EnvelopeMaker> envelope_to_send,
      std::deque<std::unique_ptr<EnvelopeMaker>>* envelopes_that_failed) = 0;

  const SizeParams size_params_;

  // When the active EnvelopeMaker surpasses this size (in bytes) we invoke
  // RequestSendSoon(). This value is set to 0.6 * max_bytes_per_envelope_
  // so that it is unlikely that we ever need to return kFull because the
  // active Envelope is full.
  const size_t envelope_send_threshold_size_;

  // When the total amount of Observation data surpasses this size
  // we invoke RequestSendSoon(). This value is set to 0.6 * max_bytes_total_
  // so that it is unlikely that we ever need to return kFull because the
  // the total amount of Observation data is too great.
  const size_t total_bytes_send_threshold_;

  const ScheduleParams schedule_params_;
  const EnvelopeMakerParams envelope_maker_params_;

 protected:
  const SendRetryerParams send_retryer_params_;

  SendRetryerInterface* send_retryer_;  // not owned

 private:
  // Variables accessed only by the worker thread. These are not
  // protected by a mutex.
  std::chrono::system_clock::time_point next_scheduled_send_time_;

  std::deque<std::unique_ptr<EnvelopeMaker>> envelopes_to_send_;

 protected:
  send_retryer::CancelHandle cancel_handle_;  // Not protected by a mutex. Only
                                              // accessed by the worker thread.

 private:
  // The background worker thread that runs the method "Run()."
  std::thread worker_thread_;

  // A struct that contains a mutex and all the fields we want to protect
  // with that mutex.
  struct MutexProtectedFields {
    // Protects access to all variables below.
    std::mutex mutex;

    // Variables accessed by the worker thread and the API
    // threads. These are protected by |mutex|.

    bool expedited_send_requested = false;

    // This is the EnvelopeMaker into which new Observations are added.
    std::unique_ptr<EnvelopeMaker> active_envelope_maker;

    // RequestSendSoon() enqueues callbacks here just in case
    // active_envelope_maker is not empty. These callbacks will be invoked
    // with the result of the next send attempt that includes
    // active_envelope_maker.
    std::vector<SendCallback> on_deck_send_callback_queue;

    // The queue of callbacks that will be invoked when the next send
    // attempt completes. The worker thread moves callbacks from
    // on_deck_send_callback_queue to this queue at the same time that
    // it picks up the active_envelope_maker. RequestSendSoon() enqueues
    // callbacks directly here rather than onto on_deck_send_callback_queue
    // just in case active_envelope_maker is empty but
    // envelopes_to_send_total_bytes != 0.
    std::vector<SendCallback> current_send_callback_queue;

    // Keeps track of the sum of the sizes of all Envelopes in
    // |envelopes_to_send_|.
    size_t envelopes_to_send_total_bytes = 0;

    // Set shut_down to true in order to stop "Run()".
    bool shut_down = false;

    // Setting this to true indicates that the total size of all Observations
    // currently stored in the ShippingManager is too large. We will stop
    // accepting any more Observations until this is set to false again.
    bool temporarily_full = false;

    // We initialize idle_ and waiting_for_schedule_ to true because initially
    // the worker thread isn't even started so WaitUntilIdle() and
    // WaitUntilWorkerWaiting() should return immediately if invoked. We will
    // set them to false in Start().
    bool idle = true;
    bool waiting_for_schedule = true;

    // These diagnostic stats are mostly useful in a testing environment but
    // may possibly prove useful in production also.
    size_t num_send_attempts = 0;
    size_t num_failed_attempts = 0;
    grpc::Status last_send_status;

    std::condition_variable add_observation_notifier;
    std::condition_variable expedited_send_notifier;
    std::condition_variable shutdown_notifier;
    std::condition_variable idle_notifier;
    std::condition_variable waiting_for_schedule_notifier;
  };

  // Constructing a LockedFields object constructs a std::unique_lock and
  // therefore locks the mutex in |fields|.
  struct LockedFields {
    explicit LockedFields(MutexProtectedFields* fields)
        : lock(fields->mutex), fields(fields) {}
    std::unique_lock<std::mutex> lock;
    MutexProtectedFields* fields;  // not owned.
  };

  // All of the fields that are accessed by multiple threads and therefore
  // need to be protected by a mutex. Do not access this variable directly.
  // Instead invoke the method lock() which returns a smart pointer to a
  // |LockedFields| that wraps this field. All access to this field should
  // be via the following idiom:
  //
  // auto locked = lock();
  // locked->fields->[field_name].
  MutexProtectedFields _mutex_protected_fields_do_not_access_directly_;

 protected:
  // Provides access to the fields that are protected by a mutex while
  // acquiring a lock on the mutex. Destroy the returned std::unique_ptr to
  // release the mutex.
  std::unique_ptr<LockedFields> lock() {
    return std::unique_ptr<LockedFields>(
        new LockedFields(&_mutex_protected_fields_do_not_access_directly_));
  }

 private:
  // Does the work of TakeActiveEnvelopeMaker() and assumes that the
  // fields->mutex lock is held.
  std::unique_ptr<EnvelopeMaker> TakeActiveEnvelopeMakerLockHeld(
      MutexProtectedFields* fields);

  // Does the work of RequestSendSoon() and assumes that the fields->mutex lock
  // is held.
  void RequestSendSoonLockHeld(MutexProtectedFields* fields);

  // Helper method used by Run(). Assumes the fields->mutex lock is held.
  void PrepareForSendLockHeld(MutexProtectedFields* fields);
};

class LegacyShippingManager : public ShippingManager {
 public:
  LegacyShippingManager(const SizeParams& size_params,
                        const ScheduleParams& scheduling_params,
                        const EnvelopeMakerParams& envelope_maker_params,
                        const SendRetryerParams send_retryer_params,
                        SendRetryerInterface* send_retryer);

 private:
  void SendEnvelopeToBackend(
      std::unique_ptr<EnvelopeMaker> envelope_to_send,
      std::deque<std::unique_ptr<EnvelopeMaker>>* envelopes_that_failed);
};

class ClearcutV1ShippingManager : public ShippingManager {
 public:
  ClearcutV1ShippingManager(
      const SizeParams& size_params, const ScheduleParams& scheduling_params,
      const EnvelopeMakerParams& envelope_maker_params,
      const SendRetryerParams send_retryer_params,
      SendRetryerInterface* send_retryer,
      std::unique_ptr<::clearcut::ClearcutUploader> clearcut);

 private:
  void SendEnvelopeToBackend(
      std::unique_ptr<EnvelopeMaker> envelope_to_send,
      std::deque<std::unique_ptr<EnvelopeMaker>>* envelopes_that_failed);

  std::mutex clearcut_mutex_;
  std::unique_ptr<::clearcut::ClearcutUploader> clearcut_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_SHIPPING_MANAGER_H_
