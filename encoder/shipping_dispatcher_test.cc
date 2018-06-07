// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encoder/shipping_dispatcher.h"

#include <deque>
#include <set>
#include <string>
#include <utility>

#include "./gtest.h"
#include "config/client_config.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/project_context.h"
// Generated from shipping_dispatcher_test_config.yaml
#include "encoder/shipping_dispatcher_test_config.h"

namespace cobalt {
namespace encoder {

typedef ObservationMetadata::ShufflerBackend ShufflerBackend;
using config::ClientConfig;
using tensorflow_statusor::StatusOr;

namespace {

// These values must match the values specified in the invocation of
// generate_test_config_h() in CMakeLists.txt. and in the invocation of
// cobalt_config_header("generate_shipping_manager_test_config") in BUILD.gn.
const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;

const size_t kNoOpEncodingByteOverhead = 30;
const size_t kMaxBytesPerObservation = 50;
const size_t kMaxBytesPerEnvelope = 200;
const size_t kMaxBytesTotal = 1000;

// Note(rudominer) Because kMinEnvelopeSendSize = 170 and
// kMaxBytesPerEnvelope = 200, and our tests use Observations of size
// 40 bytes, the worker thread will attempt to send Envelopes that contain
// exactly 5, 40-byte Observations. (4 * 40 < 170 and 6 * 40 > 200 ).
const size_t kMinEnvelopeSendSize = 170;
const std::chrono::seconds kMaxSeconds = ShippingManager::kMaxSeconds;

// Returns a ProjectContext obtained by parsing the configuration specified
// in shipping_manager_test_config.yaml
std::shared_ptr<ProjectContext> GetTestProject() {
  // Parse the base64-encoded, serialized CobaltConfig in
  // shipping_manager_test_config.h. This is generated from
  // shipping_manager_test_config.yaml. Edit that yaml file to make changes. The
  // variable name below, |cobalt_config_base64|, must match what is
  // specified in the build files.
  std::unique_ptr<ClientConfig> client_config =
      ClientConfig::CreateFromCobaltConfigBase64(cobalt_config_base64);
  EXPECT_NE(nullptr, client_config);

  return std::shared_ptr<ProjectContext>(new ProjectContext(
      kCustomerId, kProjectId,
      std::shared_ptr<ClientConfig>(client_config.release())));
}

class FakeSystemData : public SystemDataInterface {
 public:
  FakeSystemData() {
    system_profile_.set_os(SystemProfile::FUCHSIA);
    system_profile_.set_arch(SystemProfile::ARM_64);
    system_profile_.set_board_name("Fake Board Name");
  }

  const SystemProfile& system_profile() const override {
    return system_profile_;
  };

  static void CheckSystemProfile(const Envelope& envelope) {
    // SystemProfile is not placed in the envelope at this time.
    EXPECT_EQ(SystemProfile::UNKNOWN_OS, envelope.system_profile().os());
    EXPECT_EQ(SystemProfile::UNKNOWN_ARCH, envelope.system_profile().arch());
    EXPECT_EQ("", envelope.system_profile().board_name());
  }

 private:
  SystemProfile system_profile_;
};

class FakeHTTPClient : public clearcut::HTTPClient {
 public:
  explicit FakeHTTPClient(std::set<uint32_t>* seen_event_codes)
      : clearcut::HTTPClient(), seen_event_codes_(seen_event_codes) {}

  std::future<StatusOr<clearcut::HTTPResponse>> Post(
      clearcut::HTTPRequest request,
      std::chrono::steady_clock::time_point _ignored) {
    clearcut::LogRequest req;
    req.ParseFromString(request.body);
    for (auto event : req.log_event()) {
      seen_event_codes_->insert(event.event_code());
    }

    clearcut::HTTPResponse response;
    response.http_code = 200;
    clearcut::LogResponse resp;
    resp.SerializeToString(&response.response);

    std::promise<StatusOr<clearcut::HTTPResponse>> response_promise;
    response_promise.set_value(std::move(response));

    return response_promise.get_future();
  }

 private:
  std::set<uint32_t>* seen_event_codes_;
};

class FakeShippingManager : public ShippingManager {
 public:
  FakeShippingManager(std::chrono::seconds schedule_interval,
                      std::chrono::seconds min_interval)
      : ShippingManager(
            ShippingManager::SizeParams(kMaxBytesPerObservation,
                                        kMaxBytesPerEnvelope, kMaxBytesTotal,
                                        kMinEnvelopeSendSize),
            ShippingManager::ScheduleParams(schedule_interval, min_interval),
            ShippingManager::EnvelopeMakerParams("", EncryptedMessage::NONE, "",
                                                 EncryptedMessage::NONE)),
        envelopes_sent_(0) {}

  void SendEnvelopeToBackend(
      std::unique_ptr<EnvelopeMaker> envelope_to_send,
      std::deque<std::unique_ptr<EnvelopeMaker>>* envelopes_that_failed) {
    std::lock_guard<std::mutex> lock(mutex_);
    envelopes_sent_ += 1;
  }

  int32_t envelopes_sent() {
    std::lock_guard<std::mutex> lock(mutex_);
    return envelopes_sent_;
  }

 private:
  std::mutex mutex_;
  int32_t envelopes_sent_;
};

}  // namespace

class ShippingDispatcherTest : public ::testing::Test {
 public:
  ShippingDispatcherTest()
      : shipping_dispatcher_(new ShippingDispatcher()),
        project_(GetTestProject()),
        encoder_(project_, ClientSecret::GenerateNewSecret(), &system_data_) {}

 protected:
  void Init(std::chrono::seconds schedule_interval,
            std::chrono::seconds min_interval) {
    shipping_dispatcher_->Register(
        ObservationMetadata::LEGACY_BACKEND,
        std::make_unique<FakeShippingManager>(schedule_interval, min_interval));
    shipping_dispatcher_->Register(
        ObservationMetadata::V1_BACKEND,
        std::make_unique<FakeShippingManager>(schedule_interval, min_interval));
    shipping_dispatcher_->Start();
  }

  ShippingManager::Status AddObservation(size_t num_bytes, uint32_t metric_id) {
    CHECK(num_bytes > kNoOpEncodingByteOverhead) << " num_bytes=" << num_bytes;
    Encoder::Result result = encoder_.EncodeString(
        metric_id, kNoOpEncodingId,
        std::string("x", num_bytes - kNoOpEncodingByteOverhead));
    return shipping_dispatcher_->AddObservation(*result.observation,
                                                std::move(result.metadata));
  }

  FakeShippingManager* Manager(ShufflerBackend backend) {
    return reinterpret_cast<FakeShippingManager*>(
        shipping_dispatcher_->manager(backend).ConsumeValueOrDie());
  }

  FakeSystemData system_data_;
  std::set<uint32_t> seen_clearcut_event_codes_;
  std::unique_ptr<ShippingDispatcher> shipping_dispatcher_;
  std::shared_ptr<ProjectContext> project_;
  Encoder encoder_;
};

TEST_F(ShippingDispatcherTest, ConstructAndDestruct) {
  Init(kMaxSeconds, kMaxSeconds);
}

TEST_F(ShippingDispatcherTest, SendObservationToDefault) {
  Init(kMaxSeconds, std::chrono::seconds::zero());

  EXPECT_EQ(ShippingManager::kOk, AddObservation(40, kDefaultMetricId));

  shipping_dispatcher_->RequestSendSoon();
  shipping_dispatcher_->WaitUntilIdle(kMaxSeconds);

  EXPECT_EQ(1, Manager(ObservationMetadata::LEGACY_BACKEND)->envelopes_sent());
  EXPECT_EQ(0, Manager(ObservationMetadata::V1_BACKEND)->envelopes_sent());
}

TEST_F(ShippingDispatcherTest, SendObservationToLegacy) {
  Init(kMaxSeconds, std::chrono::seconds::zero());

  EXPECT_EQ(ShippingManager::kOk, AddObservation(40, kLegacyMetricId));

  shipping_dispatcher_->RequestSendSoon();
  shipping_dispatcher_->WaitUntilIdle(kMaxSeconds);

  EXPECT_EQ(1, Manager(ObservationMetadata::LEGACY_BACKEND)->envelopes_sent());
  EXPECT_EQ(0, Manager(ObservationMetadata::V1_BACKEND)->envelopes_sent());
}

TEST_F(ShippingDispatcherTest, SendObservationToV1Backend) {
  Init(kMaxSeconds, std::chrono::seconds::zero());

  EXPECT_EQ(ShippingManager::kOk, AddObservation(40, kClearcutMetricId));

  shipping_dispatcher_->RequestSendSoon();
  shipping_dispatcher_->WaitUntilIdle(kMaxSeconds);

  EXPECT_EQ(0, Manager(ObservationMetadata::LEGACY_BACKEND)->envelopes_sent());
  EXPECT_EQ(1, Manager(ObservationMetadata::V1_BACKEND)->envelopes_sent());
}

TEST_F(ShippingDispatcherTest, DistributeObservationsProperly) {
  Init(kMaxSeconds, std::chrono::seconds::zero());

  EXPECT_EQ(ShippingManager::kOk, AddObservation(40, kDefaultMetricId));
  shipping_dispatcher_->RequestSendSoon();
  shipping_dispatcher_->WaitUntilIdle(kMaxSeconds);

  EXPECT_EQ(ShippingManager::kOk, AddObservation(40, kLegacyMetricId));
  shipping_dispatcher_->RequestSendSoon();
  shipping_dispatcher_->WaitUntilIdle(kMaxSeconds);

  EXPECT_EQ(ShippingManager::kOk, AddObservation(40, kClearcutMetricId));
  shipping_dispatcher_->RequestSendSoon();
  shipping_dispatcher_->WaitUntilIdle(kMaxSeconds);

  EXPECT_EQ(2, Manager(ObservationMetadata::LEGACY_BACKEND)->envelopes_sent());
  EXPECT_EQ(1, Manager(ObservationMetadata::V1_BACKEND)->envelopes_sent());
}

}  // namespace encoder
}  // namespace cobalt
