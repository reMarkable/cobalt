// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_SEND_RETRYER_H_
#define COBALT_ENCODER_SEND_RETRYER_H_

#include <memory>

#include "encoder/clock.h"
#include "encoder/shuffler_client.h"

namespace cobalt {
namespace encoder {

namespace send_retryer {

// An object that provides a way to cancel an invocation of SendToShuffler().
class CancelHandle;

// An abstract interface implemented by SendRetryer (below).
// This is abstracted so that it may be mocked in tests.
class SendRetryerInterface {
 public:
  virtual ~SendRetryerInterface() = default;

  virtual grpc::Status SendToShuffler(
      std::chrono::seconds initial_rpc_deadline,
      std::chrono::seconds overerall_deadline,
      send_retryer::CancelHandle* cancel_handle,
      const EncryptedMessage& encrypted_message) = 0;
};

// This class wraps a ShufflerClient with retry logic. We categorize
// gRPC error statuses as either retryable or not. If an error is retryable
// then we retry with exponential backoff, otherwise we give up. If the
// returned error is DEADLINE_EXEEDED then we increase the deadline.
class SendRetryer : public SendRetryerInterface {
 public:
  // Does not take ownership of |shuffler_client|.
  explicit SendRetryer(ShufflerClientInterface* shuffler_client);

  virtual ~SendRetryer() = default;

  // Uses the wrapped ShufflerClient to send the given |encrypted_message| to
  // the Shuffler. It should be an encrypted Envelope as given by the output of
  // EnvelopeMaker::MakeEncryptedEnvelope().
  //
  // |inital_rpc_deadline| is the gRPC deadline to use for the first send
  // attempt. This must be positive or we will CHECK fail. The deadline will be
  // increased in later attempts if a DEADLINE_EXCEEDED status code is returned
  // from the Shuffler. We will not honor arbitrarily large values of this
  // parameter: We will truncate to a reasonable upper bound for all
  // RPC timeouts.
  //
  // |overall_deadline| is the overall deadline granted to the Retryer for its
  // multiple attempts to send. This  must be >= |initial_rpc_deadline| or
  // we will CHECK fail. This may be set to std::chrono::seconds:max()
  // and the Retryer will retry "forever". Normally this should not be set
  // to less than about a minute in order to give the Retryer enough time
  // to try multiple times with increasing timeouts.
  //
  // |cancel_handle|. An optional pointer to an object that allows for
  // cancellation. This may be NULL but if it is not NULL then it must remain
  // valid for the duration of the call. Does not take ownership of
  // |cancel_handle|.
  //
  // This is a synchronous method that may take a long time to return
  // as the Retryer performs multiple attempts to send with exponential
  // backoff. This method will return when one of the following occurs:
  //
  // - A successful send. Returns OK.
  //
  // - A non-retryable status code is received from the shuffler. Returns
  //   that status code.
  //
  // - |overall_deadline| has been exceeded. Returns DEADLINE_EXCEEDED.
  //
  // - TryCancel() is invoked (from some other thread) on the provided
  //   |cancel_handle|. May return CANCELLED in this case if the call
  //   was successfully canceled. Other responses including OK are possible
  //   after a TryCancel() because the cancellation is not guaranteed.
  grpc::Status SendToShuffler(
      std::chrono::seconds initial_rpc_deadline,
      std::chrono::seconds overerall_deadline,
      send_retryer::CancelHandle* cancel_handle,
      const EncryptedMessage& encrypted_message) override;

 private:
  friend class SendRetryerTest;

  ShufflerClientInterface* shuffler_client_;  // not owned

  // The value with which we will initialize sleep_between_attempts. This
  // is exposed to friend tests so that they can set it to a smaller value.
  std::chrono::milliseconds initial_sleep_ = std::chrono::milliseconds(1000);

  // The clock is abstracted so that friend tests can set a non-system clock.
  std::unique_ptr<ClockInterface> clock_;
};

// An object that provides a way to cancel an invocation of SendToShuffler().
class CancelHandle {
 public:
  // Attempt to cancel the call. This may or may not succeed depending on
  // the current state of the call. If the Retryer is currently blocked
  // waiting for a retry then this will succeed. If a gRPC call is in-flight
  // an attempt will be made to cancel it but this may not succeed.
  void TryCancel();

 private:
  friend class SendRetryer;
  friend class SendRetryerTest;

  std::mutex mutex_;
  bool cancelled_ = false;
  std::condition_variable cancel_notifier_;
  std::unique_ptr<grpc::ClientContext> context_;

  // If this is not NULL then it will be invoked with the value
  // |sleep_millis| just prior to a sleep of |sleep_millis| milliseconds
  // commencing. This is only used for tests so far but may prove useful
  // for other purposes in the future.
  std::function<void(int)> sleep_notification_function_;
};

}  // namespace send_retryer
}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_SEND_RETRYER_H_
