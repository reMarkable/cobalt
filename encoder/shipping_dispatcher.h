// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_SHIPPING_DISPATCHER_H_
#define COBALT_ENCODER_SHIPPING_DISPATCHER_H_

#include <map>
#include <memory>
#include <vector>

#include "./observation.pb.h"
#include "encoder/shipping_manager.h"
#include "third_party/clearcut/uploader.h"
#include "third_party/tensorflow_statusor/statusor.h"
#include "util/status.h"

namespace cobalt {
namespace encoder {

// ShippingDispatcher is a wrapper around the ShippingManager class. It allows
// dispatching to multiple different ShippingManagers so that we can send
// observations to different backends (current backends are a GKE GRPC, and a
// dummy Clearcut uploader). See ShippingManager for more details.
class ShippingDispatcher {
 public:
  // Registers a ShippingManager to be handled by the ShippingDispatcher. A
  // particular |backend| should not be registered more than once, if it is,
  // the last call to Register will take precedence.
  void Register(ObservationMetadata::ShufflerBackend backend,
                std::unique_ptr<ShippingManager> manager);

  // Returns the list of ObservationMetadata::ShufflerBackends that have been
  // registered with the ShippingDispatcher.
  std::vector<ObservationMetadata::ShufflerBackend> RegisteredBackends();

  // Starts the worker thread for all of the ShippingManagers. This method must
  // be invoked exactly once.
  void Start();

  // Notifies all of the ShippingManagers that an observation may have been
  // added to their ObservationStores.
  void NotifyObservationsAdded();

  // Register a request with all controlled ShippingManagers for an expedited
  // send. The underlying ShippingManager's worker thread will use its
  // |SendRetryer| to send all of the accumulated, unsent Observations as soon
  // as possible but not sooner than |min_interval| seconds after the previous
  // send operation has completed.
  void RequestSendSoon();

  // A version of RequestSendSoon() that provides feedback about the send.
  // |send_callback| will be invoked with the result of the requested send
  // attempt. More precisely, send_callback will be invoked after all of the
  // ShippingManagers have attempted to send all of the Observations that were
  // added prior to the invocation of RequestSendSoon(). It will be invoked
  // with true if all such Observations were succesfully sent. It will be
  // invoked with false if some Observations were not able to be sent, but
  // the status of any particular Observation may not be determined. This
  // is useful mainly in tests.
  void RequestSendSoon(ShippingManager::SendCallback send_callback);

  // Waits for |max_wait| seconds on each owned ShippingManager in sequence
  // until each becomes idle.This method is most useful if it can be arranged
  // that there are no concurrent invocations of AddObservation() (for example
  // in a test) because such concurrent invocations may cause the idle state to
  // never be entered.
  void WaitUntilIdle(std::chrono::seconds max_wait);

  // These diagnostic stats are mostly useful in a testing environment but
  // may possibly prove useful in production also.
  size_t NumSendAttempts();
  size_t NumFailedAttempts();
  util::Status last_send_status(ObservationMetadata::ShufflerBackend backend);

 private:
  friend class ShippingDispatcherTest;
  tensorflow_statusor::StatusOr<ShippingManager*> manager(
      ObservationMetadata::ShufflerBackend backend);

  std::map<ObservationMetadata::ShufflerBackend,
           std::unique_ptr<ShippingManager>>
      shipping_managers_;

  // RequestSendCallback is used in RequestSendSoon to make sure that the
  // callback is called only after a specified number of invocations of |Call|.
  class RequestSendCallback {
   public:
    ~RequestSendCallback();
    RequestSendCallback(ShippingManager::SendCallback cb,
                        size_t needed_callbacks);
    void Call(bool success);

   private:
    const size_t needed_callbacks_;
    size_t seen_callbacks_;
    bool success_;
    bool callback_called_;
    ShippingManager::SendCallback cb_;
    std::mutex mutex_;
  };
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_SHIPPING_DISPATCHER_H_
