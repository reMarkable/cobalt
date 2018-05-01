// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "encoder/shipping_dispatcher.h"
#include "third_party/tensorflow_statusor/status_macros.h"

namespace cobalt {
namespace encoder {

typedef ObservationMetadata::ShufflerBackend ShufflerBackend;
using tensorflow_statusor::StatusOr;

namespace {

util::Status ConvertToStatus(const grpc::Status& status) {
  return util::Status((util::StatusCode)status.error_code(),
                      status.error_message(), status.error_details());
}

}  // namespace

ShippingDispatcher::RequestSendCallback::RequestSendCallback(
    ShippingManager::SendCallback cb, size_t needed_callbacks)
    : needed_callbacks_(needed_callbacks),
      seen_callbacks_(0),
      success_(true),
      callback_called_(false),
      cb_(cb) {
  if (needed_callbacks_ == 0) {
    callback_called_ = true;
    cb_(true);
  }
}

ShippingDispatcher::RequestSendCallback::~RequestSendCallback() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!callback_called_) {
    // This should never happen, so return a failure.
    cb_(false);
  }
}

void ShippingDispatcher::RequestSendCallback::Call(bool success) {
  std::lock_guard<std::mutex> lock(mutex_);
  seen_callbacks_++;
  success_ &= success;
  if (seen_callbacks_ == needed_callbacks_) {
    callback_called_ = true;
    cb_(success_);
  }
}

void ShippingDispatcher::Register(ShufflerBackend backend,
                                  std::unique_ptr<ShippingManager> manager) {
  shipping_managers_[backend] = std::move(manager);
}

std::vector<ShufflerBackend> ShippingDispatcher::RegisteredBackends() {
  std::vector<ShufflerBackend> backends;
  for (auto& backend : shipping_managers_) {
    backends.push_back(backend.first);
  }
  return backends;
}

void ShippingDispatcher::Start() {
  for (auto& manager : shipping_managers_) {
    manager.second->Start();
  }
}

void ShippingDispatcher::RequestSendSoon() {
  for (auto& manager : shipping_managers_) {
    manager.second->RequestSendSoon();
  }
}

void ShippingDispatcher::RequestSendSoon(
    ShippingManager::SendCallback send_callback) {
  auto cb = std::make_shared<RequestSendCallback>(send_callback,
                                                  shipping_managers_.size());
  for (auto& manager : shipping_managers_) {
    manager.second->RequestSendSoon(
        [this, cb](bool success) { cb->Call(success); });
  }
}

void ShippingDispatcher::WaitUntilIdle(std::chrono::seconds max_wait) {
  for (auto& manager : shipping_managers_) {
    manager.second->WaitUntilIdle(max_wait);
  }
}

ShippingManager::Status ShippingDispatcher::AddObservation(
    const Observation& observation,
    std::unique_ptr<ObservationMetadata> metadata) {
  auto shipping_manager_or = manager(metadata->backend());
  if (!shipping_manager_or.ok()) {
    VLOG(4) << "Unable to send to unregistered backend: "
            << metadata->backend();
    return ShippingManager::Status::kOk;
  }
  ShippingManager* shipping_manager = shipping_manager_or.ConsumeValueOrDie();
  return shipping_manager->AddObservation(
      observation, std::make_unique<ObservationMetadata>(*metadata));
}

util::Status ShippingDispatcher::last_send_status(ShufflerBackend backend) {
  ShippingManager* m;
  CB_ASSIGN_OR_RETURN(m, manager(backend));
  return ConvertToStatus(m->last_send_status());
}

StatusOr<std::unique_ptr<EnvelopeMaker>>
ShippingDispatcher::TakeActiveEnvelopeMaker(ShufflerBackend backend) {
  ShippingManager* m;
  CB_ASSIGN_OR_RETURN(m, manager(backend));
  return m->TakeActiveEnvelopeMaker();
}

size_t ShippingDispatcher::NumSendAttempts() {
  size_t result = 0;
  for (auto& manager : shipping_managers_) {
    result += manager.second->num_send_attempts();
  }
  return result;
}

size_t ShippingDispatcher::NumFailedAttempts() {
  size_t result = 0;
  for (auto& manager : shipping_managers_) {
    result += manager.second->num_failed_attempts();
  }
  return result;
}

StatusOr<ShippingManager*> ShippingDispatcher::manager(
    ShufflerBackend backend) {
  if (shipping_managers_.find(backend) == shipping_managers_.end()) {
    std::ostringstream ss;
    ss << "Could not find shipping manager for backend #" << backend;
    return util::Status(util::StatusCode::NOT_FOUND, ss.str());
  } else {
    return shipping_managers_[backend].get();
  }
}

}  // namespace encoder
}  // namespace cobalt
