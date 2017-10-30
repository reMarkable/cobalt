// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encoder/send_retryer.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./encrypted_message.pb.h"
#include "./gtest.h"
#include "./logging.h"
#include "encoder/clock.h"
#include "encoder/shuffler_client.h"
#include "third_party/gflags/include/gflags/gflags.h"

namespace cobalt {
namespace encoder {
namespace send_retryer {

namespace {

// An implementation of ShufflerClientInterface that returns the sequence
// of statuses it is told to return and records the number of times that
// SendToShuffler() was invoked and the gRPC deadlines in each invocation.
// Additionally, the client will optionally invoke cancel_handle->TryCancel() on
// a specified invocation count number.
class FakeShufflerClient : public ShufflerClientInterface {
 public:
  FakeShufflerClient(IncrementingClock* incrementing_clock,
                     CancelHandle* cancel_handle)
      : incrementing_clock(incrementing_clock), cancel_handle(cancel_handle) {}

  ~FakeShufflerClient() {}

  grpc::Status SendToShuffler(const EncryptedMessage& encrypted_message,
                              grpc::ClientContext* context = nullptr) override {
    call_count++;
    CHECK(context);
    // The gRPC deadline embedded in |context| is expressed as an absolute
    // deadline. We recover the value of |rpc_deadline| set by the
    // Retryer by subtracting the clock's current time using |peek_now()|.
    // This is a bit fragile: It works only because we know that there have
    // been no invocations of clock_->now() between the time that the the
    // Retryer computed the deadline and the call to this function. We
    // record the deadline as a number of milliseconds.
    deadlines.push_back(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            context->deadline() - incrementing_clock->peek_now())
            .count());

    CHECK(call_count <= statuses_to_return.size());
    if (call_count == cancel_on_this_call_count) {
      cancel_handle->TryCancel();
    }
    return statuses_to_return[call_count - 1];
  }

  IncrementingClock* incrementing_clock;  // not owned.
  std::vector<grpc::Status> statuses_to_return = {grpc::Status::OK};
  size_t call_count = 0;
  std::vector<int64_t> deadlines;
  size_t cancel_on_this_call_count = -1;
  CancelHandle* cancel_handle;
};

}  // namespace

class SendRetryerTest : public ::testing::Test {
 public:
  SendRetryerTest() {
    std::unique_ptr<IncrementingClock> clock(new IncrementingClock());
    incrementing_clock_ = clock.get();
    cancel_handle_.reset(new CancelHandle());
    cancel_handle_->sleep_notification_function_ = [this](int sleep_millis) {
      sleep_millis_used_.push_back(sleep_millis);
    };
    shuffler_client_.reset(
        new FakeShufflerClient(incrementing_clock_, cancel_handle_.get()));
    retryer_.reset(new SendRetryer(shuffler_client_.get()));
    retryer_->clock_ = std::move(clock);
    retryer_->initial_sleep_ = std::chrono::milliseconds(1);
  }

 protected:
  // Invokes SendToShuffler() with an initial_rpc_deadline of 10 seconds,
  // the given overall_deadline (also defaulting to 10 seconds), and our fixed
  // cancel_handle and encrypted_message.
  grpc::Status SendToShuffler(
      std::chrono::seconds overerall_deadline = std::chrono::seconds(10)) {
    return retryer_->SendToShuffler(std::chrono::seconds(10),
                                    overerall_deadline, cancel_handle_.get(),
                                    encrypted_message_);
  }

  // Checks that the status, call_count and deadlines are as expected.
  void CheckResults(grpc::Status status, grpc::StatusCode expected_code,
                    uint32_t expected_call_count,
                    std::vector<int> expected_deadline_seconds) {
    EXPECT_EQ(expected_code, status.error_code());
    EXPECT_EQ(expected_call_count, shuffler_client_->call_count);
    ASSERT_EQ(expected_call_count, shuffler_client_->deadlines.size());
    ASSERT_EQ(expected_call_count, expected_deadline_seconds.size());
    for (size_t i = 0; i < expected_call_count; i++) {
      EXPECT_EQ(1000 * expected_deadline_seconds[i],
                shuffler_client_->deadlines[i])
          << "i=" << i << ", expected_seconds=" << expected_deadline_seconds[i]
          << ", actual_millis=" << shuffler_client_->deadlines[i];
    }
    // Check that the sleep times between send attempts started at the
    // expected initial value and doubled each time.
    ASSERT_EQ(expected_call_count - 1, sleep_millis_used_.size());
    // expected_sleep_millis is initialized to 1 because in the constructor for
    // SendRetryierTest we invoked
    // retryer_->initial_sleep_ = std::chrono::milliseconds(1);
    int expected_sleep_millis = 1;
    for (size_t i = 0; i < expected_call_count - 1; i++) {
      EXPECT_EQ(expected_sleep_millis, sleep_millis_used_[i]);
      expected_sleep_millis *= 2;
    }
  }

  std::unique_ptr<FakeShufflerClient> shuffler_client_;
  std::unique_ptr<SendRetryer> retryer_;
  EncryptedMessage encrypted_message_;
  IncrementingClock* incrementing_clock_;  // not owned.
  std::unique_ptr<CancelHandle> cancel_handle_;
  std::vector<int> sleep_millis_used_;
};

// Tests that when the ShufflerClient returns OK the first time then the
// Retryer returns OK and does not retry.
TEST_F(SendRetryerTest, ReturnsOkIn1) {
  auto status = SendToShuffler();
  // Expect 1 call with a deadline of 10 seconds to return OK.
  CheckResults(status, grpc::OK, 1u, {10u});
}

// Tests that when the ShufflerClient retruns a non-retryable status code
// the first time then the Retryer return it and does not retry.
TEST_F(SendRetryerTest, ReturnsInvalidArgIn1) {
  shuffler_client_->statuses_to_return = {
      grpc::Status(grpc::INVALID_ARGUMENT, "Invalid Argument")};
  auto status = SendToShuffler();

  // Expect 1 call with a deadline of 10 seconds to return INVALID_ARGUMENT.
  CheckResults(status, grpc::INVALID_ARGUMENT, 1u, {10u});
}

// Tests that when the ShufflerClient returns ABORTED the first time and OK
// the second time then the Retryer tries a total of 2 times and returns OK.
TEST_F(SendRetryerTest, ReturnsAbortedThenOk) {
  shuffler_client_->statuses_to_return = {
      grpc::Status(grpc::ABORTED, "Aborted"), grpc::Status::OK};
  auto status = SendToShuffler(std::chrono::seconds::max());

  // Expect 2 call with deadlines seconds {10, 10} to return OK.
  CheckResults(status, grpc::OK, 2u, {10u, 10u});
}

// Tests that when the ShufflerClient returns UNAVAILABLE the first time and
// INVALID_ARGUMENT the second time then the Retryer tries a total of 2 times
// and returns INVALID_ARGUMENT.
TEST_F(SendRetryerTest, ReturnsUnavailableThenInvalidArgument) {
  shuffler_client_->statuses_to_return = {
      grpc::Status(grpc::UNAVAILABLE, "UNAVAILABLE"),
      grpc::Status(grpc::INVALID_ARGUMENT, "Invalid Argument")};
  auto status = SendToShuffler(std::chrono::seconds::max());

  // Expect 2 call with deadlines seconds {10, 10} to return INVALID_ARGUMENT.
  CheckResults(status, grpc::INVALID_ARGUMENT, 2u, {10u, 10u});
}

// Tests that when the ShufflerClient returns ABORTED the first time and
// then INTERNAL the second time and then OK the third time then the Retryer
// tries a total of 3 times and returns OK.
TEST_F(SendRetryerTest, ReturnsAbortedThenInternalThenOk) {
  shuffler_client_->statuses_to_return = {
      grpc::Status(grpc::ABORTED, "Aborted"),
      grpc::Status(grpc::INTERNAL, "Internal"), grpc::Status::OK};
  auto status = SendToShuffler(std::chrono::seconds::max());

  // Expect 3 call with deadlines seconds {10, 10, 10} to return OK.
  CheckResults(status, grpc::OK, 3u, {10u, 10u, 10u});
}

// Tests that when the ShufflerClient returns DEADLINE_EXCEEDED multiple times
// then OK, the Retryer increases the RPC deadline by a factor of 1.5 each time.
TEST_F(SendRetryerTest, ReturnsDeadlineExceededTwiceThenOk) {
  shuffler_client_->statuses_to_return = {
      grpc::Status(grpc::DEADLINE_EXCEEDED, "DEADLINE_EXCEEDED"),
      grpc::Status(grpc::DEADLINE_EXCEEDED, "DEADLINE_EXCEEDED"),
      grpc::Status(grpc::DEADLINE_EXCEEDED, "DEADLINE_EXCEEDED"),
      grpc::Status(grpc::DEADLINE_EXCEEDED, "DEADLINE_EXCEEDED"),
      grpc::Status::OK};
  auto status = SendToShuffler(std::chrono::seconds::max());

  CheckResults(status, grpc::OK, 5u, {10u, 15u, 23u, 35u, 53u});
}

// Tests that the Retryer quits when the overall deadline is reached.
TEST_F(SendRetryerTest, DeadlineExceededAfterOne) {
  // Each time clock_->now() is invoked it will be 10 seconds later than
  // the previous time.
  incrementing_clock_->set_increment(std::chrono::seconds(10));

  // Instruct the FakeShufflerClient to return firt DEADLINE_EXCEEDED and
  // then OK. But it will never get a chance to return OK because it will
  // only be invoked once.
  shuffler_client_->statuses_to_return = {
      grpc::Status(grpc::DEADLINE_EXCEEDED, "DEADLINE_EXCEEDED"),
      grpc::Status::OK};

  // Invoke SendToShuffler() with an overall deadline of 21s.
  // Note that SendToShuffler() invokes clock_->now() twice in the while loop:
  // once before the send and once after the send. So after the first send it
  // will be 20 (simulated) seconds later than the start time and the overall
  // deadline will be within one second of expring and so there won't be a
  // second send.
  auto status = SendToShuffler(std::chrono::seconds(21));

  // After the first DEADLINE_EXCEEDED the Retryer will give up and
  // return DEADLINE_EXCEEDED. We expect only one attempt with a gRPC
  // deadline of 10s.
  CheckResults(status, grpc::DEADLINE_EXCEEDED, 1u, {10u});
}

// Tests that the Retryer quits when the overall deadline is reached.
TEST_F(SendRetryerTest, DeadlineExceededAfterTwo) {
  // Each time clock_->now() is invoked it will be 10 seconds later than
  // the previous time.
  incrementing_clock_->set_increment(std::chrono::seconds(10));

  // Instruct the FakeShufflerClient to return DEADLINE_EXCEEDED twice and
  // then OK. But it will never get a chance to return OK because it will
  // only be invoked twice.
  shuffler_client_->statuses_to_return = {
      grpc::Status(grpc::DEADLINE_EXCEEDED, "DEADLINE_EXCEEDED"),
      grpc::Status(grpc::DEADLINE_EXCEEDED, "DEADLINE_EXCEEDED"),
      grpc::Status::OK};

  // Invoke SendToShuffler() with an overall deadline of 25s.
  // Note that SendToShuffler() invokes clock_->now() twice in the while loop:
  // once before the send and once after the send. So after the first send it
  // will be 20 (simulated) seconds later than the start time and there will
  // be 5 seconds left for the overall deadline. The sleep times we are using
  // are negligable so the expected rpc timeout for the second send is 5s.
  auto status = SendToShuffler(std::chrono::seconds(25));

  // After the second DEADLINE_EXCEEDED the Retryer will give up and return
  // DEADLINE_EXCEEDED. We expecte two attempts with gRPC deadlines of 10s
  // and 5s respectively.
  CheckResults(status, grpc::DEADLINE_EXCEEDED, 2u, {10u, 5u});
}

// Tests that when the ShufflerClient returns DEADLINE_EXCEEDED multiple times
// then OK, the Retryer increases the RPC deadline by a factor of 1.5 each time.
TEST_F(SendRetryerTest, TestCancel) {
  // Instruct the FakeShufflerClient to return DEADLINE_EXCEEDED 4 times and
  // then return OK.
  shuffler_client_->statuses_to_return = {
      grpc::Status(grpc::DEADLINE_EXCEEDED, "DEADLINE_EXCEEDED"),
      grpc::Status(grpc::DEADLINE_EXCEEDED, "DEADLINE_EXCEEDED"),
      grpc::Status(grpc::DEADLINE_EXCEEDED, "DEADLINE_EXCEEDED"),
      grpc::Status(grpc::DEADLINE_EXCEEDED, "DEADLINE_EXCEEDED"),
      grpc::Status::OK};
  // But also instruct it to invoke TryCancel() on the CancelHandle after
  // the second call to Send().
  shuffler_client_->cancel_on_this_call_count = 2;

  auto status = SendToShuffler(std::chrono::seconds::max());

  // We expect Send() to have been invoked twice with deadlines of 10s and 15,
  // and then for the Retryer to notice the cancellation and return CANCELLED.
  CheckResults(status, grpc::CANCELLED, 2u, {10u, 15u});
}

}  // namespace send_retryer
}  // namespace encoder
}  // namespace cobalt
