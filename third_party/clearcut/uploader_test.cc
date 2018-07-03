// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>

#include "third_party/clearcut/uploader.h"

#include "./gtest.h"
#include "./logging.h"
#include "third_party/clearcut/clearcut.pb.h"
#include "third_party/gflags/include/gflags/gflags.h"

namespace clearcut {

using cobalt::util::StatusCode;

namespace {

static std::set<uint32_t> seen_event_codes;
static int next_request_wait_millis;
static bool fail_next_request;

class TestLogger : public ClearcutUploader {
 public:
  TestLogger(const std::string &url, std::unique_ptr<HTTPClient> client)
      : ClearcutUploader(url, std::move(client)) {}

  Status LogClearcutDemoEvent(uint32_t event_code, int32_t max_retries = 1) {
    LogRequest request;
    request.set_log_source(kClearcutDemoSource);
    request.add_log_event()->set_event_code(event_code);
    return UploadEvents(&request, max_retries);
  }
};

class TestHTTPClient : public HTTPClient {
 public:
  std::future<StatusOr<HTTPResponse>> Post(
      HTTPRequest request, std::chrono::steady_clock::time_point _ignored) {
    if (fail_next_request) {
      fail_next_request = false;
      std::promise<StatusOr<HTTPResponse>> response_promise;
      response_promise.set_value(
          Status(StatusCode::DEADLINE_EXCEEDED, "Artificial post failure"));
      return response_promise.get_future();
    }

    LogRequest req;
    req.ParseFromString(request.body);
    for (auto event : req.log_event()) {
      seen_event_codes.insert(event.event_code());
    }

    HTTPResponse response;
    response.http_code = 200;
    LogResponse resp;
    if (next_request_wait_millis != -1) {
      resp.set_next_request_wait_millis(next_request_wait_millis);
    }
    resp.SerializeToString(&response.response);

    std::promise<StatusOr<HTTPResponse>> response_promise;
    response_promise.set_value(std::move(response));

    return response_promise.get_future();
  }
};

class LoggerTest : public ::testing::Test {
  void SetUp() {
    seen_event_codes = {};
    next_request_wait_millis = -1;
    fail_next_request = false;
    auto unique_client = std::make_unique<TestHTTPClient>();
    client = unique_client.get();
    logger = std::make_unique<TestLogger>("http://test.com",
                                          std::move(unique_client));
  }

 public:
  std::unique_ptr<TestLogger> logger;
  TestHTTPClient *client;
};

bool SawEventCode(uint32_t event_code) {
  return seen_event_codes.find(event_code) != seen_event_codes.end();
}

}  // namespace

TEST_F(LoggerTest, BasicClearcutDemoUpload) {
  ASSERT_TRUE(logger->LogClearcutDemoEvent(1).ok());
  ASSERT_TRUE(logger->LogClearcutDemoEvent(2).ok());
  ASSERT_TRUE(logger->LogClearcutDemoEvent(3).ok());
  ASSERT_TRUE(logger->LogClearcutDemoEvent(4).ok());

  ASSERT_TRUE(SawEventCode(1));
  ASSERT_TRUE(SawEventCode(2));
  ASSERT_TRUE(SawEventCode(3));
  ASSERT_TRUE(SawEventCode(4));
}

TEST_F(LoggerTest, RateLimitingWorks) {
  next_request_wait_millis = 100;
  ASSERT_TRUE(logger->LogClearcutDemoEvent(100).ok());
  ASSERT_TRUE(SawEventCode(100));

  ASSERT_FALSE(logger->LogClearcutDemoEvent(150).ok());

  // With no pause, the second upload should fail.
  ASSERT_FALSE(SawEventCode(150));

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ASSERT_FALSE(logger->LogClearcutDemoEvent(151).ok());

  // We haven't waited long enough yet.
  ASSERT_FALSE(SawEventCode(151));

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ASSERT_TRUE(logger->LogClearcutDemoEvent(152).ok());

  // Now it should work.
  ASSERT_TRUE(SawEventCode(152));
}

TEST_F(LoggerTest, ShouldRetryOnFailedUpload) {
  fail_next_request = true;
  ASSERT_TRUE(logger->LogClearcutDemoEvent(1, 2).ok());
  ASSERT_TRUE(SawEventCode(1));

  fail_next_request = true;
  ASSERT_FALSE(logger->LogClearcutDemoEvent(2).ok());
  ASSERT_TRUE(logger->LogClearcutDemoEvent(3).ok());
  ASSERT_TRUE(SawEventCode(3));
}

}  // namespace clearcut

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  INIT_LOGGING(argv[0]);
  return RUN_ALL_TESTS();
}
