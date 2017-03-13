// Copyright 2017 The Fuchsia Authors
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
#import "tools/observation_querier/observation_querier.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "analyzer/store/memory_store.h"
#include "analyzer/store/observation_store.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

using analyzer::store::DataStore;
using analyzer::store::MemoryStore;
using analyzer::store::ObservationStore;

namespace {

const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 2;
const uint32_t kDayIndex = 3;

}  // namespace

// Tests of ObservationQuerier.
class ObservationQuerierTest : public ::testing::Test {
 public:
  ObservationQuerierTest()
      : data_store_(new MemoryStore()),
        observation_store_(new ObservationStore(data_store_)),
        querier_(kCustomerId, kProjectId, observation_store_, &output_stream_) {
  }

 protected:
  // Clears the contents of the ObservationQuerier's output stream and returns
  // the contents prior to clearing.
  std::string ClearOutput() {
    std::string s = output_stream_.str();
    output_stream_.str("");
    return s;
  }

  // Does the current contents of the ObservationQuerier's output stream contain
  // the given text.
  bool OutputContains(const std::string text) {
    return -1 != output_stream_.str().find(text);
  }

  // Is the ObservationQuerier's output stream curently empty?
  bool NoOutput() { return output_stream_.str().empty(); }

  // Writes an observation for the given |metric_id| into the ObservationStore.
  // There will be |num_parts| parts named part0, part1, etc. Each part will
  // have the same value given by |data|. If |forculus| is true all the
  // parts will be Forculus observations otherwise all the parts will be
  // Basic RAPPOR observations.
  void WriteObservation(uint32_t metric_id, bool forculus, int num_parts,
                        std::string data) {
    ObservationMetadata metadata;
    metadata.set_customer_id(kCustomerId);
    metadata.set_project_id(kProjectId);
    metadata.set_metric_id(metric_id);
    metadata.set_day_index(kDayIndex);
    std::vector<Observation> observations;
    observations.emplace_back();
    Observation& observation = observations.back();
    for (int i = 0; i < num_parts; i++) {
      std::ostringstream stream;
      stream << "part" << i;
      std::string part_name = stream.str();
      ObservationPart& part = (*observation.mutable_parts())[part_name];
      if (forculus) {
        part.mutable_forculus()->set_ciphertext(data);
      } else {
        part.mutable_basic_rappor()->set_data(data);
      }
    }
    EXPECT_EQ(analyzer::store::kOK,
              observation_store_->AddObservationBatch(metadata, observations));
  }

  // The output stream that the ObservationQuerier has been given.
  std::ostringstream output_stream_;

  std::shared_ptr<DataStore> data_store_;
  std::shared_ptr<ObservationStore> observation_store_;

  // The ObservationQuerier under test.
  ObservationQuerier querier_;
};

// Tests processing a bad command line.
TEST_F(ObservationQuerierTest, ProcessCommandLineBad) {
  EXPECT_TRUE(querier_.ProcessCommandLine("this is not a command"));
  EXPECT_TRUE(OutputContains("Unrecognized command: this"));
}

// Tests processing the "help" command
TEST_F(ObservationQuerierTest, ProcessCommandLineHelp) {
  EXPECT_TRUE(querier_.ProcessCommandLine("help"));
  // We don't want to test the actual output too rigorously because that would
  // be a very fragile test. Just doing a sanity test.
  EXPECT_TRUE(OutputContains("Print this help message."));
  EXPECT_TRUE(OutputContains("Query up to <max_num> observations."));
}

// Tests processing a bad set command line.
TEST_F(ObservationQuerierTest, ProcessCommandLineSetBad) {
  EXPECT_TRUE(querier_.ProcessCommandLine("set"));
  EXPECT_TRUE(OutputContains("Malformed set command."));
  ClearOutput();

  EXPECT_TRUE(querier_.ProcessCommandLine("set a b c"));
  EXPECT_TRUE(OutputContains("Malformed set command."));
  ClearOutput();

  EXPECT_TRUE(querier_.ProcessCommandLine("set a b"));
  EXPECT_TRUE(OutputContains("a is not a settable parameter"));
  ClearOutput();

  EXPECT_TRUE(querier_.ProcessCommandLine("set metric b"));
  EXPECT_TRUE(OutputContains("Expected positive integer instead of b."));
  ClearOutput();

  ClearOutput();
}

// Tests processing the set and ls commands
TEST_F(ObservationQuerierTest, ProcessCommandLineSetAndLs) {
  EXPECT_TRUE(querier_.ProcessCommandLine("ls"));
  EXPECT_TRUE(OutputContains("Metric ID: 1"));
  ClearOutput();

  EXPECT_TRUE(querier_.ProcessCommandLine("set metric 2"));
  EXPECT_TRUE(NoOutput());

  EXPECT_TRUE(querier_.ProcessCommandLine("ls"));
  EXPECT_TRUE(OutputContains("Metric ID: 2"));
}

// Tests processing a bad query command
TEST_F(ObservationQuerierTest, ProcessCommandLineQueryBad) {
  EXPECT_TRUE(querier_.ProcessCommandLine("query"));
  EXPECT_TRUE(
      OutputContains("Malformed query command. Expected query <max_num>"));
  ClearOutput();

  EXPECT_TRUE(querier_.ProcessCommandLine("query a b"));
  EXPECT_TRUE(
      OutputContains("Malformed query command. Expected query <max_num>"));
  ClearOutput();

  EXPECT_TRUE(querier_.ProcessCommandLine("query a"));
  EXPECT_TRUE(OutputContains("Expected positive integer instead of a."));
  ClearOutput();
}

// Tests processing the query command
TEST_F(ObservationQuerierTest, ProcessCommandLineQuery) {
  // Query when there are no observations.
  EXPECT_TRUE(querier_.ProcessCommandLine("query 3"));
  EXPECT_TRUE(NoOutput());

  // Add 3 Forculus observations for metric 1.
  uint32_t metric_id = 1;
  bool forculus = true;
  WriteObservation(metric_id, forculus, 1, "hello");
  WriteObservation(metric_id, forculus, 1, "goodbye");
  WriteObservation(metric_id, forculus, 1, "peace");

  // Add 2 basic RAPPOR observations for metric 2.
  metric_id = 2;
  forculus = false;
  WriteObservation(metric_id, forculus, 1, "A");
  WriteObservation(metric_id, forculus, 1, "B");

  // Query 3 observations from metric 1.
  EXPECT_TRUE(querier_.ProcessCommandLine("query 3"));
  EXPECT_TRUE(
      OutputContains("part0:forculus:ciphertext:aGVsbG8=::point_x:"));
  EXPECT_TRUE(
      OutputContains("part0:forculus:ciphertext:Z29vZGJ5ZQ==::point_x:"));
  EXPECT_TRUE(
      OutputContains("part0:forculus:ciphertext:cGVhY2U=::point_x:"));
  ClearOutput();

  // Query 3 observations from metric 2.
  querier_.ProcessCommandLine("set metric 2");
  EXPECT_TRUE(querier_.ProcessCommandLine("query 3"));
  EXPECT_TRUE(
      OutputContains("part0:basic_rappor:01000010"));
  EXPECT_TRUE(
      OutputContains("part0:basic_rappor:01000001"));
}

// Tests processing the "quit" command
TEST_F(ObservationQuerierTest, ProcessCommandLineQuit) {
  EXPECT_FALSE(querier_.ProcessCommandLine("quit"));
  EXPECT_TRUE(NoOutput());
}

}  // namespace cobalt

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  return RUN_ALL_TESTS();
}
