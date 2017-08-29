// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "encoder/send_retryer.h"

#include "./logging.h"
#include "encoder/clock.h"
#include "encoder/shuffler_client.h"

namespace cobalt {
namespace encoder {
namespace send_retryer {

namespace {

// We won't ever attempt an RPC with a deadline of more than 80 seconds.
// gRPC  has a bound on how large a message can be and within this
// bound an RPC should always take far less than this amount of time.
constexpr std::chrono::seconds kMaxRpcDeadline = std::chrono::seconds(80);

// Returns whether or not an operation should be retried based on its returned
// status.
bool ShouldRetry(const grpc::Status& status) {
  switch (status.error_code()) {
    case grpc::ABORTED:
    case grpc::DEADLINE_EXCEEDED:
    case grpc::INTERNAL:
    case grpc::UNAVAILABLE:
      return true;
    default:
      return false;
  }
}

}  // namespace

void CancelHandle::TryCancel() {
  std::lock_guard<std::mutex> lock(mutex_);
  cancelled_ = true;
  cancel_notifier_.notify_all();
  if (context_) {
    context_->TryCancel();
  }
}

SendRetryer::SendRetryer(ShufflerClientInterface* shuffler_client)
    : shuffler_client_(shuffler_client), clock_(new SystemClock()) {
  CHECK(shuffler_client);
}

grpc::Status SendRetryer::SendToShuffler(
    std::chrono::seconds initial_rpc_deadline,
    std::chrono::seconds overall_deadline, CancelHandle* cancel_handle,
    const EncryptedMessage& encrypted_message) {
  CHECK(initial_rpc_deadline > std::chrono::seconds::zero());
  CHECK(overall_deadline >= initial_rpc_deadline);

  // If the caller wants us to use an overall deadline set the
  // absolute_deadline.
  std::chrono::system_clock::time_point absolute_deadline =
      std::chrono::system_clock::time_point::max();
  if (overall_deadline < std::chrono::seconds::max()) {
    absolute_deadline = clock_->now() + overall_deadline;
  }

  // If the caller did not pass in a CancelHandle make a local one. We will
  // need it for the grpc::ClientContext so we can set a gRPC timeout.
  std::unique_ptr<CancelHandle> local_cancel_handle;
  if (cancel_handle == NULL) {
    local_cancel_handle.reset(new CancelHandle());
    cancel_handle = local_cancel_handle.get();
  }

  // Initialize rpc_deadline to min(initial_rpc_deadline, kMaxRpcDeadline).
  std::chrono::seconds rpc_deadline = initial_rpc_deadline;
  if (rpc_deadline > kMaxRpcDeadline) {
    rpc_deadline = kMaxRpcDeadline;
  }

  // This value will increase with our exponential backoff.
  std::chrono::milliseconds sleep_between_attempts = initial_sleep_;

  // The retry loop.
  grpc::ClientContext* client_context;
  while (true) {
    {
      std::lock_guard<std::mutex> lock(cancel_handle->mutex_);

      // Quit now if we were cancelled.
      if (cancel_handle->cancelled_) {
        return grpc::Status(grpc::CANCELLED, "Cancelled from CancelHandle.");
      }

      // We need a new ClientContext for every request.
      cancel_handle->context_.reset(new grpc::ClientContext());
      client_context = cancel_handle->context_.get();
    }

    // Attempt the RPC.
    client_context->set_deadline(clock_->now() + rpc_deadline);
    auto status =
        shuffler_client_->SendToShuffler(encrypted_message, client_context);

    // If the RPC succeeded or failed with a non-retryable error then we
    // are done.
    if (!ShouldRetry(status)) {
      return status;
    }

    std::chrono::seconds time_remaining =
        std::chrono::duration_cast<std::chrono::seconds>(absolute_deadline -
                                                         clock_->now());

    // If we have less then 2 seconds remaining then quit. This is because
    // we still need to sleep before the next timeout. We want at least one
    // second to sleep and at least on second of RPC timeout after that.
    if (time_remaining < std::chrono::seconds(2)) {
      std::ostringstream stream;
      stream << "Overall deadline of " << overall_deadline.count()
             << " seconds would be exceeded";
      auto s = stream.str();
      VLOG(3) << s;
      return grpc::Status(grpc::DEADLINE_EXCEEDED, s);
    }

    // We know there are at least two seconds left before the absolute deadline.
    // We are about to sleep before the next send attempt. Limit the sleep
    // time to time_remaining - 1. We save 1 second to use as the RPC timeout.
    if (sleep_between_attempts > time_remaining - std::chrono::seconds(1)) {
      sleep_between_attempts = time_remaining - std::chrono::seconds(1);
    }

    // If we hit DEADLINE_EXCEEDED last time multiply the deadline by
    // kGrowthFactor.
    // Note(rudominer) The value kGrowthFactor = 1.51 is fairly arbitrary. We
    // wanted kGrowthFactor < 2 for smaller growth. We wanted
    // kGrowthFactor >= 1.5 to ensure that for all integers n >= 1,
    // round(n * kGrowthFactor) > n. We chose 1.51 instead of 1.5 out of
    // superstition.
    static const double kGrowthFactor = 1.5;
    if (status.error_code() == grpc::DEADLINE_EXCEEDED) {
      rpc_deadline = std::chrono::seconds(static_cast<int>(
          round(static_cast<double>(rpc_deadline.count()) * kGrowthFactor)));
    }

    // But make the deadline less than the max deadline,
    if (rpc_deadline > kMaxRpcDeadline) {
      rpc_deadline = kMaxRpcDeadline;
    }
    // and less than time_remaining - sleep_between_attempts
    if (rpc_deadline > time_remaining - sleep_between_attempts) {
      rpc_deadline =
          time_remaining - std::chrono::duration_cast<std::chrono::seconds>(
                               sleep_between_attempts);
    }

    // Note: We invoke the real system clock here, not clock_->now().
    // This is because even in a test we want to use the real
    // system clock to compute wakeup_time. This is because
    auto wakeup_time =
        std::chrono::system_clock::now() + sleep_between_attempts;

    {
      std::unique_lock<std::mutex> lock(cancel_handle->mutex_);
      if (cancel_handle->cancelled_) {
        VLOG(3) << "SendRetryer::SendToShuffler() cancelled between attempts.";
        return grpc::Status(grpc::CANCELLED, "Cancelled from CancelHandle.");
      }

      // Wait until cancelled or wakeup_time.
      auto sleep_millis = sleep_between_attempts.count();
      VLOG(3) << "Shuffler returned (" << status.error_code() << ") "
              << status.error_message() << ". We will retry after a sleep of "
              << sleep_millis << " millis.";
      if (cancel_handle->sleep_notification_function_) {
        cancel_handle->sleep_notification_function_(sleep_millis);
      }
      cancel_handle->cancel_notifier_.wait_until(
          lock, wakeup_time,
          [cancel_handle] { return cancel_handle->cancelled_; });
      if (cancel_handle->cancelled_) {
        VLOG(3) << "SendRetryer::SendToShuffler() cancelled during wait.";
        return grpc::Status(grpc::CANCELLED, "Cancelled from CancelHandle.");
      }
    }

    // Exponential backoff.
    sleep_between_attempts *= 2;
  }
}

}  // namespace send_retryer
}  // namespace encoder
}  // namespace cobalt
