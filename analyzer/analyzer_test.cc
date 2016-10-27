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

#include <gflags/gflags.h>

#include <string>
#include <map>

#include "analyzer/analyzer_service.h"
#include "analyzer/store/mem_store.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

using google::protobuf::Empty;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

namespace cobalt {
namespace analyzer {

// Fixture to start and stop the analyzer
class AnalyzerFunctionalTest : public ::testing::Test {
 public:
  AnalyzerFunctionalTest()
      : analyzer_(std::unique_ptr<Store>(new MemStore)) {}

  // Raw data backed in the in-memory store
  std::map<std::string, std::string>& store_data() {
    return MemStoreSingleton::instance().data_;
  }

 protected:
  virtual void SetUp() {
    analyzer_.Start();
  }

  virtual void TearDown() {
    analyzer_.Shutdown();
    analyzer_.Wait();
  }

  MemStore store_;
  AnalyzerServiceImpl analyzer_;
};

// We connect to the analyzer and send a test RPC and assert the result
TEST_F(AnalyzerFunctionalTest, TestGRPC) {
  char dst[1024];
  ClientContext context;

  // Connect to the analyzer
  snprintf(dst, sizeof(dst), "localhost:%d", kAnalyzerPort);

  std::shared_ptr<Channel> chan =
      grpc::CreateChannel(dst, grpc::InsecureChannelCredentials());

  std::unique_ptr<Analyzer::Stub> analyzer(Analyzer::NewStub(chan));

  // Execute the RPC
  ObservationBatch req;
  Empty resp;

  EncryptedMessage* encrypted_observation = req.add_encrypted_observation();
  encrypted_observation->set_ciphertext("hello");

  Status status = analyzer->AddObservations(&context, req, &resp);
  ASSERT_TRUE(status.ok());

  // Check that an item got inserted into the store
  ASSERT_EQ(store_data().size(), 1);

  // Grab the item that got inserted
  std::string key = store_data().begin()->first;
  std::string val;

  ASSERT_EQ(store_.get(key, &val), 0);

  // check that the item matches the observation
  std::string obs;

  encrypted_observation->SerializeToString(&obs);

  ASSERT_EQ(val, obs);
}

}  // namespace analyzer
}  // namespace cobalt
