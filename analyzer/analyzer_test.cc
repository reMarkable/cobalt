// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "analyzer/analyzer.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

namespace cobalt {
namespace analyzer {

// Fixture to start and stop the analyzer
class AnalyzerFunctionalTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    analyzer_.Start();
  }

  virtual void TearDown() {
    analyzer_.Shutdown();
    analyzer_.Wait();
  }

 private:
  AnalyzerServiceImpl analyzer_;
};

// We connect to the analyzer and send a test RPC and assert the result
TEST_F(AnalyzerFunctionalTest, TestGRPC) {
  char dst[1024];
  const char *kTestMsg = "test_message";
  ClientContext context;

  // Connect to the analyzer
  snprintf(dst, sizeof(dst), "localhost:%d", kAnalyzerPort);

  std::shared_ptr<Channel> chan =
      grpc::CreateChannel(dst, grpc::InsecureChannelCredentials());

  std::unique_ptr<Analyzer::Stub> analyzer(Analyzer::NewStub(chan));

  // Execute the RPC
  EchoMsg req, reply;

  req.set_msg(kTestMsg);

  Status status = analyzer->EchoTest(&context, req, &reply);
  ASSERT_TRUE(status.ok());

  // Check the result
  ASSERT_STREQ(reply.msg().c_str(), kTestMsg);
}

}  // namespace analyzer
}  // namespace cobalt
