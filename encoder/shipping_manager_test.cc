// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encoder/shipping_manager.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./gtest.h"
#include "./logging.h"
#include "config/config_text_parser.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/project_context.h"
#include "third_party/gflags/include/gflags/gflags.h"

namespace cobalt {
namespace encoder {

using config::EncodingRegistry;
using config::MetricRegistry;
using send_retryer::CancelHandle;
using send_retryer::SendRetryer;

namespace {

const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;
const uint32_t kMetricId = 1;
const uint32_t kEncodingConfigId = 1;
const size_t kNoOpEncodingByteOverhead = 30;
const size_t kMaxBytesPerObservation = 50;
const size_t kMaxBytesPerEnvelope = 200;
const size_t kMaxBytesTotal = 1000;
// Note(rudominer) Because kMinEnvelopeSendSize = 170 and
// kMaxBytesPerEnvelope = 200, and our tests use Observations of size
// 40 bytes, the worker thread will attempt to send Envelopes that contain
// exactly 5, 40-byte Observations. (4 * 40 < 170 and 6 * 40 > 200 ).
const size_t kMinEnvelopeSendSize = 170;
const std::chrono::seconds kInitialRpcDeadline(10);
const std::chrono::seconds kDeadlinePerSendAttempt(60);
const std::chrono::seconds kMaxSeconds = ShippingManager::kMaxSeconds;

const char* kMetricConfigText = R"(
# Metric 1 has one string part.
element {
  customer_id: 1
  project_id: 1
  id: 1
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
    }
  }
}
)";

const char* kEncodingConfigText = R"(
# EncodingConfig 2 is NoOp.
element {
  customer_id: 1
  project_id: 1
  id: 1
  no_op_encoding {
  }
}

)";

// Returns a ProjectContext obtained by parsing the above configuration
// text strings.
std::shared_ptr<ProjectContext> GetTestProject() {
  // Parse the metric config string
  auto metric_parse_result =
      config::FromString<RegisteredMetrics>(kMetricConfigText, nullptr);
  EXPECT_EQ(config::kOK, metric_parse_result.second);
  std::shared_ptr<MetricRegistry> metric_registry(
      metric_parse_result.first.release());

  // Parse the encoding config string
  auto encoding_parse_result =
      config::FromString<RegisteredEncodings>(kEncodingConfigText, nullptr);
  EXPECT_EQ(config::kOK, encoding_parse_result.second);
  std::shared_ptr<EncodingRegistry> encoding_registry(
      encoding_parse_result.first.release());

  return std::shared_ptr<ProjectContext>(new ProjectContext(
      kCustomerId, kProjectId, metric_registry, encoding_registry));
}

class FakeSystemData : public SystemDataInterface {
 public:
  FakeSystemData() {
    system_profile_.set_os(SystemProfile::FUCHSIA);
    system_profile_.set_arch(SystemProfile::ARM_64);
    system_profile_.mutable_cpu()->set_vendor_name("Fake Vendor Name");
    system_profile_.mutable_cpu()->set_signature(1234567);
  }

  const SystemProfile& system_profile() const override {
    return system_profile_;
  };

  static void CheckSystemProfile(const Envelope& envelope) {
    // SystemProfile is not placed in the envelope at this time.
    EXPECT_EQ(SystemProfile::UNKNOWN_OS, envelope.system_profile().os());
    EXPECT_EQ(SystemProfile::UNKNOWN_ARCH, envelope.system_profile().arch());
    EXPECT_EQ("", envelope.system_profile().cpu().vendor_name());
    EXPECT_EQ(0u, envelope.system_profile().cpu().signature());
  }

 private:
  SystemProfile system_profile_;
};

struct FakeSendRetryer : public SendRetryerInterface {
  grpc::Status SendToShuffler(
      std::chrono::seconds initial_rpc_deadline,
      std::chrono::seconds overerall_deadline,
      send_retryer::CancelHandle* cancel_handle,
      const EncryptedMessage& encrypted_message) override {
    // Decrypt encrypted_message. (No actual decryption is involved since
    // we used the NONE encryption scheme.)
    util::MessageDecrypter decrypter("");
    Envelope recovered_envelope;
    EXPECT_TRUE(
        decrypter.DecryptMessage(encrypted_message, &recovered_envelope));
    EXPECT_EQ(1, recovered_envelope.batch_size());
    EXPECT_EQ(kMetricId, recovered_envelope.batch(0).meta_data().metric_id());
    FakeSystemData::CheckSystemProfile(recovered_envelope);

    std::unique_lock<std::mutex> lock(mutex);
    send_call_count++;
    observation_count +=
        recovered_envelope.batch(0).encrypted_observation_size();
    // We grab the return value before we block. This allows the test
    // thread to wait for us to block, then change the value of
    // status_to_return for the *next* send without changing it for
    // the currently blocking send.
    grpc::Status status = status_to_return;
    if (should_block) {
      is_blocking = true;
      send_is_blocking_notifier.notify_all();
      send_can_exit_notifier.wait(lock, [this] { return !should_block; });
      is_blocking = false;
    }
    return status;
  }

  std::mutex mutex;
  bool should_block = false;
  std::condition_variable send_can_exit_notifier;
  bool is_blocking = false;
  std::condition_variable send_is_blocking_notifier;
  grpc::Status status_to_return = grpc::Status::OK;
  int send_call_count = 0;
  int observation_count = 0;
};

}  // namespace

class ShippingManagerTest : public ::testing::Test {
 public:
  ShippingManagerTest()
      : project_(GetTestProject()),
        encoder_(project_, ClientSecret::GenerateNewSecret(), &system_data_) {}

 protected:
  void Init(std::chrono::seconds schedule_interval,
            std::chrono::seconds min_interval) {
    send_retryer_.reset(new FakeSendRetryer());
    shipping_manager_.reset(new ShippingManager(
        ShippingManager::SizeParams(kMaxBytesPerObservation,
                                    kMaxBytesPerEnvelope, kMaxBytesTotal,
                                    kMinEnvelopeSendSize),
        ShippingManager::ScheduleParams(schedule_interval, min_interval),
        ShippingManager::EnvelopeMakerParams("", EncryptedMessage::NONE, "",
                                             EncryptedMessage::NONE),
        ShippingManager::SendRetryerParams(kInitialRpcDeadline,
                                           kDeadlinePerSendAttempt),
        send_retryer_.get()));
    shipping_manager_->Start();
  }

  ShippingManager::Status AddObservation(size_t num_bytes) {
    CHECK(num_bytes > kNoOpEncodingByteOverhead) << " num_bytes=" << num_bytes;
    Encoder::Result result = encoder_.EncodeString(
        kMetricId, kEncodingConfigId,
        std::string("x", num_bytes - kNoOpEncodingByteOverhead));
    return shipping_manager_->AddObservation(*result.observation,
                                             std::move(result.metadata));
  }

  void CheckCallCount(int expected_call_count, int expected_observation_count) {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    EXPECT_EQ(expected_call_count, send_retryer_->send_call_count);
    EXPECT_EQ(expected_observation_count, send_retryer_->observation_count);
  }

  FakeSystemData system_data_;
  std::unique_ptr<FakeSendRetryer> send_retryer_;
  std::unique_ptr<ShippingManager> shipping_manager_;
  std::shared_ptr<ProjectContext> project_;
  Encoder encoder_;
};

// We construct a ShippingManager and destruct it without calling any methods.
// This tests that the destructor requests that the worker thread terminate
// and then waits for it to terminate.
TEST_F(ShippingManagerTest, ConstructAndDestruct) {
  Init(kMaxSeconds, kMaxSeconds);
}

// We construct a ShippingManager and add one small Observation to it.
// Before the ShippingManager has a chance to send the Observation we
// destruct it. We test that the Add() returns OK and the destructor
// succeeds.
TEST_F(ShippingManagerTest, AddOneObservationAndDestruct) {
  Init(kMaxSeconds, kMaxSeconds);
  EXPECT_EQ(ShippingManager::kOk, AddObservation(40));
}

// We add one Observation, confirm that it is not immediately sent,
// invoke RequestSendSoon, wait for the Observation to be sent, confirm
// that it was sent.
TEST_F(ShippingManagerTest, SendOne) {
  // Init with a very long time for the regular schedule interval but
  // zero for the minimum interval so the test doesn't have to wait.
  Init(kMaxSeconds, std::chrono::seconds::zero());
  // Add one Observation().
  EXPECT_EQ(ShippingManager::kOk, AddObservation(40));

  // Confirm it has not been sent yet.
  CheckCallCount(0, 0);

  // Invoke RequestSendSoon.
  shipping_manager_->RequestSendSoon();

  // Wait for it to be sent.
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // Confirm it has been sent.
  EXPECT_EQ(1u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(0u, shipping_manager_->num_failed_attempts());
  EXPECT_EQ(grpc::OK, shipping_manager_->last_send_status().error_code());
  CheckCallCount(1, 1);
}

// We add two Observations, confirm that they are not immediately sent,
// invoke RequestSendSoon, wait for the Observations to be sent, confirm
// that they were sent together in a single Envelope.
TEST_F(ShippingManagerTest, SendTwo) {
  // Init with a very long time for the regular schedule interval but
  // zero for the minimum interval so the test doesn't have to wait.
  Init(kMaxSeconds, std::chrono::seconds::zero());

  // Add two observations.
  EXPECT_EQ(ShippingManager::kOk, AddObservation(40));
  EXPECT_EQ(ShippingManager::kOk, AddObservation(40));

  // Confirm they have not been sent.
  CheckCallCount(0, 0);

  // Request send soon.
  shipping_manager_->RequestSendSoon();

  // Wait for both Observations to be sent.
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // Confirm the two Observations were sent together in a single Envelope.
  EXPECT_EQ(1u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(0u, shipping_manager_->num_failed_attempts());
  EXPECT_EQ(grpc::OK, shipping_manager_->last_send_status().error_code());
  CheckCallCount(1, 2);
}

// Trys to add an Observation that is too big. Tests that kObservationTooBig
// is returned.
TEST_F(ShippingManagerTest, ObservationTooBig) {
  // Init with a very long time for the regular schedule interval but
  // zero for the minimum interval so the test doesn't have to wait.
  Init(kMaxSeconds, std::chrono::seconds::zero());

  // Add one observation that is too big.
  EXPECT_EQ(ShippingManager::kObservationTooBig, AddObservation(60));
}

// The value of |envelope_send_threshold_size_| is 60% * max_bytes_per_envelope
// = 60% * 200 = 120 bytes.
//
// We add two 40 byte observations and expect them not be be sent.
// Then we add the third 40 byte observation pushing the byte count
// over the threshold. This triggers RequestSendSoon(). All three
// 40 byte observations should be sent in one envelope.
TEST_F(ShippingManagerTest, EnvelopeSendThresholdSize) {
  // Init with a very long time for the regular schedule interval but
  // zero for the minimum interval so the test doesn't have to wait.
  Init(kMaxSeconds, std::chrono::seconds::zero());

  // Add one Observation.
  EXPECT_EQ(ShippingManager::kOk, AddObservation(40));
  CheckCallCount(0, 0);

  // Add another Observation.
  EXPECT_EQ(ShippingManager::kOk, AddObservation(40));
  CheckCallCount(0, 0);

  // Add another Observation. This pushes us over the threshold.
  EXPECT_EQ(ShippingManager::kOk, AddObservation(40));

  // Now all Observations should be sent. Wait for them.
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // All three Observations should have been sent in a single Envelope.
  EXPECT_EQ(1u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(0u, shipping_manager_->num_failed_attempts());
  EXPECT_EQ(grpc::OK, shipping_manager_->last_send_status().error_code());
  CheckCallCount(1, 3);
}

// Add multiple Observations and allow them to be sent on the regular
// schedule.
TEST_F(ShippingManagerTest, ScheduledSend) {
  // We set both schedule_interval_ and min_interval_ to zero so the test
  // does not have to wait.
  Init(std::chrono::seconds::zero(), std::chrono::seconds::zero());

  // Add two Observations but do not invoke RequestSendSoon() and do
  // not add enough Observations to exceed envelope_send_threshold_size_.
  for (int i = 0; i < 2; i++) {
    EXPECT_EQ(ShippingManager::kOk, AddObservation(40));
  }
  // Wait for the scheduled send
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // We do not check the number of sends because that depends on the
  // timing interaction of the test thread and the worker thread and so it
  // would be flaky. Just check that all 3 Observations were sent.
  std::unique_lock<std::mutex> lock(send_retryer_->mutex);
  EXPECT_EQ(2, send_retryer_->observation_count);
  EXPECT_EQ(grpc::OK, shipping_manager_->last_send_status().error_code());
}

// Tests that if we manage to exceed max_bytes_per_envelope then
// the ShippingManager will return kFull.
TEST_F(ShippingManagerTest, ExceedMaxBytesPerEnvelope) {
  // We configure the worker thread to not be able to do any work
  // so no sending will occur.
  Init(kMaxSeconds, kMaxSeconds);
  // We can add 5 40-byte Observations.
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(ShippingManager::kOk, AddObservation(40));
  }
  // But the sixth causes us to exceed max_bytes_per_envelope.
  EXPECT_EQ(ShippingManager::kFull, AddObservation(40));
}

// Tests that if we manage to exceed max_bytes_total but not
// max_bytes_per_envelope then the ShippingManager will return kFull.
// Also tests the ShippingManager's algorithm for combining small Envelopes
// into larger Envelopes before sending.
TEST_F(ShippingManagerTest, ExceedMaxBytesTotal) {
  // Init with a very long time for the regular schedule interval but
  // zero for the minimum interval so the test doesn't have to wait.
  Init(kMaxSeconds, std::chrono::seconds::zero());

  // Configure the FakeSendRetryer to fail every time.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::CANCELLED;
  }

  // kMaxBytesTotal = 1000 and we are using Observations of size 40 bytes.
  // 40 * 25 = 1000 so the first Observation that causes us to exceed
  // max_bytes_total_ is the 26th and we allow this one to be added before
  // setting temporarily_full_ true.
  //
  // Add 26 Observations. We want to do this in such a way that we don't
  // exceed max_bytes_per_envelope_.  Each time we will invoke RequestSendSoon()
  // and then WaitUntilWorkerWaiting() so that we know that between
  // invocations of AddObservtion() the worker thread will complete one
  // execution of SendAllEnvelopes().
  for (int i = 0; i < 26; i++) {
    EXPECT_EQ(ShippingManager::kOk, AddObservation(40));
    if (i < 15) {
      // After having added 15 Observations we have exceeded
      // total_bytes_send_threshold (see the test TotalBytesSendThreshold below
      // for that computation) and this means that each invocation of
      // AddObservation() automatically invokes RequestSendSoon() and so we
      // don't want to invoke it again here.
      shipping_manager_->RequestSendSoon();
    }
    shipping_manager_->WaitUntilWorkerWaiting(kMaxSeconds);
    EXPECT_TRUE(shipping_manager_->num_send_attempts() > (size_t)i);
    EXPECT_EQ(shipping_manager_->num_send_attempts(),
              shipping_manager_->num_failed_attempts());
    EXPECT_EQ(grpc::CANCELLED,
              shipping_manager_->last_send_status().error_code());
  }

  // We expect there to have been 81 calls to SendRetryer::SendToShuffler()
  // in which the Envelopes sent contained a total of 351 Observations. Here
  // we explain how this was calculated. See the comments at the top of the
  // file on kMinEnvelopeSendSize. There it is explained that the
  // ShippingManager will attempt to bundle together up to 5 Observations into
  // a single Envelope before sending. None of the sends succeed so the
  // ShippingManager keeps accumulating more Envelopes containing 5 Observations
  // that failed to send. Below is the complete pattern of send attempts. Each
  // set in braces represents one execution of SendAllEnvelopes(). The numbers
  // in each set represent the invocations of SendOneEnvelope() with an
  // Envelope that contains that many Observations.
  //
  // Thus the total number of send attempts is the total number of numbers:
  // 5 * (1 + 2 + 3 + 4 + 5) + 6 = 5 * 15 + 6 = 81.
  //
  // And the total number of Observations is the sum of all the numbers:
  // (1 + 2 + 3 + 4 + 5) * 5 + (1 + 2 + 3 + 4) * 25 + (5*5 + 1) = 351
  //
  // {1}, {2}, {3}, {4}, {5},
  // {5, 1}, {5, 2}, {5, 3}, {5, 4}, {5, 5},
  // {5, 5, 1}, ... {5, 5, 5},
  // {5, 5, 5, 1} ... {5, 5, 5, 5}
  // {5, 5, 5, 5, 1} ... {5, 5, 5, 5, 5}
  // {5, 5, 5, 5, 5, 1}
  CheckCallCount(81, 351);
  EXPECT_EQ(81u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(81u, shipping_manager_->num_failed_attempts());

  // Now attempt to add a 27th Observation and expected to get kFull because
  // we have exceeded max_bytes_total.
  EXPECT_EQ(ShippingManager::kFull, AddObservation(40));

  // Now configure the FakeSendRetryer to start succeeding,
  // and reset the counts.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::OK;
    send_retryer_->send_call_count = 0;
    send_retryer_->observation_count = 0;
  }

  // Send all of the accumulated Observations.
  shipping_manager_->RequestSendSoon();
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // All 26 successfully-added Observations should have been sent in six
  // envelopes
  CheckCallCount(6, 26);
  EXPECT_EQ(grpc::OK, shipping_manager_->last_send_status().error_code());
  EXPECT_EQ(87u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(81u, shipping_manager_->num_failed_attempts());

  // Now we can add a 27th Observation and send it.
  EXPECT_EQ(ShippingManager::kOk, AddObservation(40));
  shipping_manager_->RequestSendSoon();
  shipping_manager_->WaitUntilIdle(kMaxSeconds);
  CheckCallCount(7, 27);
  EXPECT_EQ(grpc::OK, shipping_manager_->last_send_status().error_code());
  EXPECT_EQ(88u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(81u, shipping_manager_->num_failed_attempts());
}

// Tests that when the total amount of accumulated Observation data exceeds
// total_bytes_send_threshold_  then RequestSendSoon() will be invoked.
TEST_F(ShippingManagerTest, TotalBytesSendThreshold) {
  // Init with a very long time for the regular schedule interval but
  // zero for the minimum interval so the test doesn't have to wait.
  Init(kMaxSeconds, std::chrono::seconds::zero());

  // Configure the FakeSendRetryer to fail every time so that we can accumulate
  // Observation data in memory.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::CANCELLED;
  }

  // total_bytes_send_threshold_ = 0.6 * max_bytes_total_.
  // kMaxBytesTotal = 1000 so total_bytes_send_threshold_ = 600.
  // We are using Observations of size 40 and 40 * 15 = 600 so the first
  // Observation that causes us to exceed total_bytes_send_threshold_ is #16.
  //
  // Add 15 Observations. We want to do this in such a way that we don't
  // exceed max_bytes_per_envelope_.  Each time we will invoke RequestSendSoon()
  // and then WaitUntilWorkerWaiting() so that we know that between
  // invocations of AddObservtion() the worker thread will complete one
  // execution of SendAllEnvelopes().
  for (int i = 0; i < 15; i++) {
    EXPECT_EQ(ShippingManager::kOk, AddObservation(40));
    shipping_manager_->RequestSendSoon();
    shipping_manager_->WaitUntilWorkerWaiting(kMaxSeconds);
  }

  // We expect there to have been 30 calls to SendRetryer::SendToShuffler()
  // in which the Envelopes sent contained a total of 120 Observations. Here
  // we explain how this was calculated. See the comments at the top of the
  // file on kMinEnvelopeSendSize. There it is explained that the
  // ShippingManager will attempt to bundle together up to 5 Observations into
  // a single Envelope before sending. None of the sends succeed so the
  // ShippingManager keeps accumulating more Envelopes containing 5 Observations
  // that failed to send. Below is the complete pattern of send attempts. Each
  // set in braces represents one execution of SendAllEnvelopes(). The numbers
  // in each set represent the invocations of SendOneEnvelope() with an
  // Envelope that contains that many Observations.
  //
  // Thus the total number of send attempts is the total number of numbers:
  // 5 * (1 + 2 + 3 ) = 30
  //
  // And the total number of Observations is the sum of all the numbers:
  // (1 + 2 + 3 + 4 + 5) * 3 + 5*5 + 2*5*5 = 120.
  //
  // {1}, {2}, {3}, {4}, {5},
  // {5, 1}, {5, 2}, {5, 3}, {5, 4}, {5, 5},
  // {5, 5, 1}, ... {5, 5, 5},
  CheckCallCount(30, 120);

  // Now configure the FakeSendRetryer to start succeeding,
  // and reset the counts.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::OK;
    send_retryer_->send_call_count = 0;
    send_retryer_->observation_count = 0;
  }

  // Now we send the 16th Observattion. But notice that we do *not* invoke
  // RequestSendSoon() this time. So the reason the Observations get sent
  // now is because we are exceeding total_bytes_send_threshold_.
  EXPECT_EQ(ShippingManager::kOk, AddObservation(40));

  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // All 16 Observations should have been sent in 4 envelopes as {5, 5, 5, 1}.
  CheckCallCount(4, 16);
}

// Test the version of the method RequestSendSoon() that takes a callback.
// We test that the callback is invoked with success = true when the send
// succeeds and with success = false when the send fails.
TEST_F(ShippingManagerTest, RequestSendSoonWithCallback) {
  Init(kMaxSeconds, std::chrono::seconds::zero());

  // Invoke RequestSendSoon() with a callback before any Observations are added.
  bool captured_success_arg = false;
  shipping_manager_->RequestSendSoon([&captured_success_arg](bool success) {
    captured_success_arg = success;
  });
  // Check that the callback was invoked synchronously with success = true.
  CheckCallCount(0, 0);
  EXPECT_EQ(0u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(0u, shipping_manager_->num_failed_attempts());
  EXPECT_TRUE(captured_success_arg);

  // Arrange for the first send to fail.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::CANCELLED;
  }

  // Add an Observation, invoke RequestSendSoon() with a callback.
  shipping_manager_->WaitUntilIdle(kMaxSeconds);
  EXPECT_EQ(ShippingManager::kOk,
            AddObservation(kNoOpEncodingByteOverhead + 1));
  shipping_manager_->RequestSendSoon([&captured_success_arg](bool success) {
    captured_success_arg = success;
  });
  shipping_manager_->WaitUntilWorkerWaiting(kMaxSeconds);

  // Check that the callback was invoked with success = false.
  CheckCallCount(1, 1);
  EXPECT_EQ(1u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(1u, shipping_manager_->num_failed_attempts());
  EXPECT_FALSE(captured_success_arg);

  // Arrange for the next send to succeed.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::OK;
  }

  // Don't add another Observation but invoke RequestSendSoon() with a callback.
  shipping_manager_->RequestSendSoon([&captured_success_arg](bool success) {
    captured_success_arg = success;
  });
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // Check that the callback was invoked with success = true.
  CheckCallCount(2, 2);
  EXPECT_EQ(2u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(1u, shipping_manager_->num_failed_attempts());
  EXPECT_TRUE(captured_success_arg);

  // Arrange for the next send to fail.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::CANCELLED;
  }

  // Invoke RequestSendSoon without a callback just so that there is an
  // Observation cached in the inner EnvelopeMaker.
  EXPECT_EQ(ShippingManager::kOk,
            AddObservation(kNoOpEncodingByteOverhead + 1));
  shipping_manager_->RequestSendSoon();
  shipping_manager_->WaitUntilWorkerWaiting(kMaxSeconds);
  CheckCallCount(3, 3);
  EXPECT_EQ(3u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(2u, shipping_manager_->num_failed_attempts());

  // Arrange for the next send to succeed.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::OK;
  }

  // Add an Observation, invoke RequestSendSoon() with a callback.
  EXPECT_EQ(ShippingManager::kOk,
            AddObservation(kNoOpEncodingByteOverhead + 1));
  shipping_manager_->RequestSendSoon([&captured_success_arg](bool success) {
    captured_success_arg = success;
  });
  shipping_manager_->WaitUntilIdle(kMaxSeconds);

  // Check that the callback was invoked with success = true.
  CheckCallCount(4, 5);
  EXPECT_EQ(4u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(2u, shipping_manager_->num_failed_attempts());
  EXPECT_TRUE(captured_success_arg);
}

// We test the following scenario: Suppose the worker thread is busy sending
// one batch of Observations and concurrently we invoke AddObservation()
// and RequestSendSoon() with a callback. The success status given to that
// callback must reflect the success of the later send that includes the
// newly added Observation and not the success of the send that was occurring
// concurrently. To confirm this we arrange for the first send to succeed
// and the second send to fail and check that the callback receives a |false|.
TEST_F(ShippingManagerTest,
       RequestSendSoonWithCallbackWhileWorkerThreadIsBusy) {
  Init(kMaxSeconds, std::chrono::seconds::zero());
  // Arrange that there is one Observation in the inner EnvelopeMaker
  // by arranging for the first send attempt to fail.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::CANCELLED;
  }
  EXPECT_EQ(ShippingManager::kOk,
            AddObservation(kNoOpEncodingByteOverhead + 1));
  shipping_manager_->RequestSendSoon();
  shipping_manager_->WaitUntilWorkerWaiting(kMaxSeconds);
  CheckCallCount(1, 1);
  EXPECT_EQ(1u, shipping_manager_->num_send_attempts());
  EXPECT_EQ(1u, shipping_manager_->num_failed_attempts());

  // Arrange for the next send attempt to succeed but to block until
  // we tell it to continue.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::OK;
    send_retryer_->should_block = true;
  }

  // Invoke RequestSendSoon(). This callback should receive success=true.
  bool success1;
  shipping_manager_->RequestSendSoon([&success1](bool success) {
    { success1 = success; }
  });

  // Wait for the SendRetryer() to be blocking.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->send_is_blocking_notifier.wait(
        lock, [this] { return send_retryer_->is_blocking; });
  }

  // Add an Observation and invoke RequestSendSoon() while the worker thread is
  // busy sending the first Observation.
  EXPECT_EQ(ShippingManager::kOk,
            AddObservation(kNoOpEncodingByteOverhead + 1));
  bool success2;
  shipping_manager_->RequestSendSoon([&success2](bool success) {
    { success2 = success; }
  });

  // Now release the worker thread by allowing Send() to terminate, and
  // arrange for the second send attempt to fail.
  {
    std::unique_lock<std::mutex> lock(send_retryer_->mutex);
    send_retryer_->status_to_return = grpc::Status::CANCELLED;
    send_retryer_->should_block = false;
    send_retryer_->send_can_exit_notifier.notify_all();
  }

  shipping_manager_->WaitUntilWorkerWaiting(kMaxSeconds);

  // Check that callback 1 was invoked with success=true and callback 2
  // was invoked with success = false.
  EXPECT_TRUE(success1);
  EXPECT_FALSE(success2);
}

}  // namespace encoder
}  // namespace cobalt
