// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mutex>
#include <utility>

#include "./logging.h"
#include "encoder/shipping_manager.h"

namespace cobalt {
namespace encoder {

namespace {
std::string ToString(const std::chrono::system_clock::time_point& t) {
  std::time_t time_struct = std::chrono::system_clock::to_time_t(t);
  return std::ctime(&time_struct);
}
}  // namespace

// Definition of the static constant declared in shipping_manager.h.
// This must be less than 2^31. There appears to be a bug in
// std::condition_variable::wait_for() in which setting the wait time to
// std::chrono::seconds::max() effectively sets the wait time to zero.
const std::chrono::seconds ShippingManager::kMaxSeconds(999999999);

ShippingManager::ShippingManager(
    const SizeParams& size_params, const ScheduleParams& schedule_params,
    const EnvelopeMakerParams& envelope_maker_params,
    const SendRetryerParams send_retryer_params,
    SendRetryerInterface* send_retryer)
    : size_params_(size_params),
      envelope_send_threshold_size_(
          size_t(0.6 * size_params.max_bytes_per_envelope_)),
      total_bytes_send_threshold_(size_t(0.6 * size_params.max_bytes_total_)),
      schedule_params_(schedule_params),
      envelope_maker_params_(envelope_maker_params),
      send_retryer_params_(send_retryer_params),
      send_retryer_(send_retryer),
      next_scheduled_send_time_(std::chrono::system_clock::now() +
                                schedule_params_.schedule_interval_) {
  CHECK(send_retryer);
  _mutex_protected_fields_do_not_access_directly_.active_envelope_maker.reset(
      new EnvelopeMaker(envelope_maker_params.analyzer_public_key_pem_,
                        envelope_maker_params.analyzer_scheme_,
                        envelope_maker_params.shuffler_public_key_pem_,
                        envelope_maker_params.shuffler_scheme_,
                        size_params.max_bytes_per_observation_,
                        size_params_.max_bytes_per_envelope_));
}

ShippingManager::~ShippingManager() {
  if (!worker_thread_.joinable()) {
    return;
  }
  ShutDown();
  VLOG(4) << "ShippingManager waiting for worker thread to exit...";
  worker_thread_.join();
}

void ShippingManager::Start() {
  {
    // We set idle and waiting_for_schedule to false since we are about to
    // start the worker thread. The worker thread will set these variables
    // to true at the appropriate times.
    auto locked = lock();
    locked->fields->idle = false;
    locked->fields->waiting_for_schedule = false;
  }

  std::thread t([this] { this->Run(); });
  worker_thread_ = std::move(t);
}

ShippingManager::Status ShippingManager::AddObservation(
    const Observation& observation,
    std::unique_ptr<ObservationMetadata> metadata) {
  auto locked = lock();
  if (locked->fields->shut_down) {
    return kShutDown;
  }
  if (locked->fields->temporarily_full) {
    // Not just the current EnvelopeMaker, but the ShippingManager in
    // general is full. This should be very rare. Unless there is a problem
    // with sending Observations to the server we should never be full.
    // The dynamics of when this might happen will be different once we
    // implement local persistence of Observations.
    return kFull;
  }
  switch (locked->fields->active_envelope_maker->AddObservation(
      observation, std::move(metadata))) {
    case EnvelopeMaker::kOk:
      VLOG(4) << "ShippingManager::AddObservation: OK";
      // Set idle_ false because any thread that invokes WaitUntilIdle() after
      // this should wait until the Observatoin just added has been sent.
      locked->fields->idle = false;
      break;

    case EnvelopeMaker::kObservationTooBig:
      return kObservationTooBig;

    case EnvelopeMaker::kEnvelopeFull:
      // This should be very rare because of the fact that we invoke
      // RequestSendSoon() below when size() >= envelope_send_threshold_size_.
      // This should prevent us from getting to the point that the active
      // EnvelopeMaker is ever full.
      RequestSendSoonLockHeld(locked->fields);
      return kFull;

    case EnvelopeMaker::kEncryptionFailed:
      return kEncryptionFailed;
  }
  if (locked->fields->active_envelope_maker->size() >=
      envelope_send_threshold_size_) {
    // The active EnvelopeMaker is starting to get too large. Initiate
    // a send.
    RequestSendSoonLockHeld(locked->fields);
  }

  size_t new_total_bytes = locked->fields->envelopes_to_send_total_bytes +
                           locked->fields->active_envelope_maker->size();

  if (new_total_bytes > total_bytes_send_threshold_) {
    RequestSendSoonLockHeld(locked->fields);
    if (new_total_bytes > size_params_.max_bytes_total_) {
      // Not just the current EnvelopeMaker, but the ShippingManager in general
      // is now temporarily full. This should be very rare. Unless there is
      // a problem with sending Observations to the server we should never
      // be full.
      locked->fields->temporarily_full = true;
    }
  }

  locked->fields->add_observation_notifier.notify_all();
  return kOk;
}

// The caller must hold a lock on mutex_.
void ShippingManager::RequestSendSoonLockHeld(MutexProtectedFields* fields) {
  VLOG(6) << "ShippingManager::RequestSendSoonLockHeld()";
  fields->expedited_send_requested = true;
  fields->expedited_send_notifier.notify_all();
  // We set waiting_for_schedule_ false here so that if the calling thread
  // invokes WaitUntilWorkerWaiting() after this then it will be waiting
  // for a *subsequent* time that the worker thread enters the
  // waiting-for-schedule state.
  fields->waiting_for_schedule = false;
}

void ShippingManager::RequestSendSoon() { RequestSendSoon(SendCallback()); }

void ShippingManager::RequestSendSoon(SendCallback send_callback) {
  VLOG(4) << "ShippingManager: Expedited send requested.";
  auto locked = lock();
  RequestSendSoonLockHeld(locked->fields);

  // If we were given a SendCallback then do one of three things...
  if (send_callback) {
    if (locked->fields->active_envelope_maker->size() > 0) {
      // If the active EnvelopeMaker is not empty put the SendCallback
      // onto the on-deck queue so that it gets invoked after the next
      // send that includes the active EnvelopeMaker.
      locked->fields->on_deck_send_callback_queue.push_back(send_callback);
    } else if (locked->fields->envelopes_to_send_total_bytes > 0) {
      // If the active EnvelopeMaker is empty but the worker thread has
      // some EnvelopeMakers it is currently dealing with then put the
      // SendCallback directly onto the current queue so that it gets invoked
      // after the next send attempt.
      locked->fields->current_send_callback_queue.push_back(send_callback);
    } else {
      // Otherwise the ShippingManager has no Observations so invoke the
      // SendCallback immediately and clear expedited_send_requested.
      locked->fields->expedited_send_requested = false;
      send_callback(true);
    }
  }
}

bool ShippingManager::shut_down() { return lock()->fields->shut_down; }

void ShippingManager::ShutDown() {
  {
    auto locked = lock();
    cancel_handle_.TryCancel();
    locked->fields->shut_down = true;
    locked->fields->shutdown_notifier.notify_all();
    locked->fields->add_observation_notifier.notify_all();
    locked->fields->expedited_send_notifier.notify_all();
    locked->fields->idle_notifier.notify_all();
    locked->fields->waiting_for_schedule_notifier.notify_all();
  }
  VLOG(4) << "ShippingManager: shut-down requested.";
}

std::unique_ptr<EnvelopeMaker> ShippingManager::TakeActiveEnvelopeMaker() {
  auto locked = lock();
  return TakeActiveEnvelopeMakerLockHeld(locked->fields);
}

size_t ShippingManager::num_send_attempts() {
  auto locked = lock();
  return locked->fields->num_send_attempts;
}

size_t ShippingManager::num_failed_attempts() {
  auto locked = lock();
  return locked->fields->num_failed_attempts;
}

grpc::Status ShippingManager::last_send_status() {
  auto locked = lock();
  return locked->fields->last_send_status;
}

void ShippingManager::Run() {
  while (true) {
    auto locked = lock();
    if (locked->fields->shut_down) {
      return;
    }

    // We start each iteration of the loop with a sleep of
    // schedule_params_.min_interval.
    // This ensures that we never send twice within one
    // schedule_params_.min_interval period.

    // Sleep for schedule_params_.min_interval or until shut_down_.
    VLOG(4) << "ShippingManager worker: sleeping for "
            << schedule_params_.min_interval_.count() << " seconds.";
    locked->fields->shutdown_notifier.wait_for(
        locked->lock, schedule_params_.min_interval_,
        [&locked] { return (locked->fields->shut_down); });
    VLOG(4) << "ShippingManager worker: waking up from sleep. shut_down_="
            << locked->fields->shut_down;
    if (locked->fields->shut_down) {
      return;
    }

    if (locked->fields->active_envelope_maker->Empty() &&
        locked->fields->envelopes_to_send_total_bytes == 0) {
      // There are no Observations at all in the ShippingManager. Wait
      // forever until notified that one arrived or shut down.
      VLOG(4) << "ShippingManager worker: waiting for an Observation to "
                 "arrive.";
      locked->fields->idle = true;
      locked->fields->idle_notifier.notify_all();
      locked->fields->add_observation_notifier.wait(locked->lock, [&locked] {
        return (locked->fields->shut_down ||
                !locked->fields->active_envelope_maker->Empty());
      });
      locked->fields->idle = false;
    } else {
      auto now = std::chrono::system_clock::now();
      VLOG(4) << "now: " << ToString(now) << " next_scheduled_send_time_: "
              << ToString(next_scheduled_send_time_);
      if (next_scheduled_send_time_ <= now ||
          locked->fields->expedited_send_requested) {
        VLOG(4) << "ShippingManager worker: time to send now.";
        PrepareForSendLockHeld(locked->fields);
        locked->lock.unlock();
        SendAllEnvelopes();
        next_scheduled_send_time_ = std::chrono::system_clock::now() +
                                    schedule_params_.schedule_interval_;
        locked->lock.lock();
      } else {
        // Wait until the next scheduled send time or until notified of
        // a new request for an expedited send or we are shut down.
        VLOG(4) << "ShippingManager worker: waiting "
                << schedule_params_.schedule_interval_.count()
                << " seconds for next scheduled send.";
        locked->fields->waiting_for_schedule = true;
        locked->fields->waiting_for_schedule_notifier.notify_all();
        locked->fields->expedited_send_notifier.wait_until(
            locked->lock, next_scheduled_send_time_, [&locked] {
              return (locked->fields->shut_down ||
                      locked->fields->expedited_send_requested);
            });
        locked->fields->waiting_for_schedule = false;
      }
    }
  }
}

// A lock on mutex_ should be held by the caller.
std::unique_ptr<EnvelopeMaker> ShippingManager::TakeActiveEnvelopeMakerLockHeld(
    MutexProtectedFields* fields) {
  std::unique_ptr<EnvelopeMaker> latest_envelope_maker(
      new EnvelopeMaker(envelope_maker_params_.analyzer_public_key_pem_,
                        envelope_maker_params_.analyzer_scheme_,
                        envelope_maker_params_.shuffler_public_key_pem_,
                        envelope_maker_params_.shuffler_scheme_,
                        size_params_.max_bytes_per_observation_,
                        size_params_.max_bytes_per_envelope_));
  fields->active_envelope_maker.swap(latest_envelope_maker);
  return latest_envelope_maker;
}

// A lock on mutex_ should be held by the caller.
void ShippingManager::PrepareForSendLockHeld(MutexProtectedFields* fields) {
  fields->expedited_send_requested = false;
  auto latest_envelope_maker = TakeActiveEnvelopeMakerLockHeld(fields);
  fields->envelopes_to_send_total_bytes += latest_envelope_maker->size();
  envelopes_to_send_.emplace_front(std::move(latest_envelope_maker));
  // Copy on_deck_send_callback_queue onto the end of
  // current_send_callback_queue and then clear current_send_callback_queue.
  fields->current_send_callback_queue.insert(
      fields->current_send_callback_queue.end(),
      fields->on_deck_send_callback_queue.begin(),
      fields->on_deck_send_callback_queue.end());
  fields->on_deck_send_callback_queue.clear();
}

void ShippingManager::SendAllEnvelopes() {
  std::deque<std::unique_ptr<EnvelopeMaker>> envelopes_that_failed;
  while (!envelopes_to_send_.empty()) {
    SendOneEnvelope(&envelopes_that_failed);
  }
  bool success = envelopes_that_failed.empty();
  envelopes_to_send_ = std::move(envelopes_that_failed);
  size_t envelopes_to_send_total_bytes = 0;
  for (const auto& env : envelopes_to_send_) {
    envelopes_to_send_total_bytes += env->size();
  }
  VLOG(5) << "ShippingManager: envelopes_to_send_total_bytes="
          << envelopes_to_send_total_bytes;

  std::vector<SendCallback> callbacks_to_invoke;
  {
    auto locked = lock();
    locked->fields->envelopes_to_send_total_bytes =
        envelopes_to_send_total_bytes;
    if (envelopes_to_send_total_bytes +
            locked->fields->active_envelope_maker->size() <
        size_params_.max_bytes_total_) {
      locked->fields->temporarily_full = false;
    }
    callbacks_to_invoke.swap(locked->fields->current_send_callback_queue);
  }
  for (SendCallback& callback : callbacks_to_invoke) {
    callback(success);
  }
}

void ShippingManager::SendOneEnvelope(
    std::deque<std::unique_ptr<EnvelopeMaker>>* envelopes_that_failed) {
  std::unique_ptr<EnvelopeMaker> envelope_to_send;
  size_t envelope_to_send_size = 0;
  while (!envelopes_to_send_.empty() &&
         envelope_to_send_size < size_params_.min_envelope_send_size_ &&
         envelope_to_send_size + envelopes_to_send_.front()->size() <=
             size_params_.max_bytes_per_envelope_) {
    std::unique_ptr<EnvelopeMaker> next = std::move(envelopes_to_send_.front());
    envelopes_to_send_.pop_front();
    if (!envelope_to_send) {
      envelope_to_send = std::move(next);
    } else {
      envelope_to_send->MergeOutOf(next.get());
    }
    envelope_to_send_size = envelope_to_send->size();
  }
  if (!envelope_to_send || envelope_to_send_size == 0) {
    VLOG(3) << "ShippingManager worker: There are no Observations to send.";
    return;
  }
  EncryptedMessage encrypted_envelope;
  if (!envelope_to_send->MakeEncryptedEnvelope(&encrypted_envelope)) {
    // TODO(rudominer) log
    // Drop on floor.
    return;
  }

  VLOG(5) << "ShippingManager worker: Sending Envelope of size "
          << envelope_to_send->size() << " bytes.";
  auto status = send_retryer_->SendToShuffler(
      send_retryer_params_.initial_rpc_deadline_,
      send_retryer_params_.deadline_per_send_attempt_, &cancel_handle_,
      encrypted_envelope);
  {
    auto locked = lock();
    locked->fields->num_send_attempts++;
    if (!status.ok()) {
      locked->fields->num_failed_attempts++;
    }
    locked->fields->last_send_status = status;
  }
  if (status.ok()) {
    VLOG(4) << "ShippingManager::SendOneEnvelope: OK";
    return;
  }

  // Note(rudominer) We are intentionlly using LOG instead of VLOG here because
  // we want this message to appear in the device INFO logs even if virtual
  // logging is disabled.
  LOG(INFO) << "Cobalt send to Shuffler failed: (" << status.error_code()
            << ") " << status.error_message()
            << ". Observations have been re-enqueued for later.";
  envelopes_that_failed->emplace_back(std::move(envelope_to_send));
}

void ShippingManager::WaitUntilIdle(std::chrono::seconds max_wait) {
  auto locked = lock();
  if (locked->fields->shut_down || locked->fields->idle) {
    return;
  }
  locked->fields->idle_notifier.wait_for(locked->lock, max_wait, [&locked] {
    return (locked->fields->shut_down || locked->fields->idle);
  });
}

void ShippingManager::WaitUntilWorkerWaiting(std::chrono::seconds max_wait) {
  auto locked = lock();
  if (locked->fields->shut_down || locked->fields->waiting_for_schedule) {
    return;
  }
  locked->fields->waiting_for_schedule_notifier.wait_for(
      locked->lock, max_wait, [&locked] {
        return (locked->fields->shut_down ||
                locked->fields->waiting_for_schedule);
      });
}

}  // namespace encoder
}  // namespace cobalt
