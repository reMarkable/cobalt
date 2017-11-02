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

#include "analyzer/store/observation_store.h"

#include <string>
#include <utility>

#include "analyzer/store/memory_store_test_helper.h"
#include "analyzer/store/observation_store_abstract_test.h"
#include "analyzer/store/observation_store_internal.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {
namespace store {

// Tests of the internal functions.
namespace internal {

// Tests the functions RowKey() and DayIndexFromRowKey().
TEST(ObservationStoreInteralTest, DayIndexFromRowKey) {
  std::string row_key = RowKey(39, 40, 41, 42, 43, 44);
  EXPECT_EQ(
      "0000000039:0000000040:0000000041:0000000042:00000000000000000043:"
      "0000000044",
      row_key);
  EXPECT_EQ(42u, DayIndexFromRowKey(row_key));
}

// Tests the function RangeStartKey
TEST(ObservationStoreInteralTest, RangeStartKey) {
  std::string row_key = RangeStartKey(123, 234, 345, 456);
  EXPECT_EQ(
      "0000000123:0000000234:0000000345:0000000456:00000000000000000000:"
      "0000000000",
      row_key);
}

// Tests the function RangeLimittKey
TEST(ObservationStoreInteralTest, RangeLimitKey) {
  std::string row_key = RangeLimitKey(1234, 2345, 3456, 4567);
  EXPECT_EQ(
      "0000001234:0000002345:0000003456:0000004568:00000000000000000000:"
      "0000000000",
      row_key);

  row_key = RangeLimitKey(1234, 2345, 3456, UINT32_MAX);
  EXPECT_EQ(
      "0000001234:0000002345:0000003456:4294967295:00000000000000000000:"
      "0000000000",
      row_key);
}

// Tests the function GenerateNewRowKey
TEST(ObservationStoreInteralTest, GenerateNewRowKey) {
  ObservationMetadata metadata;
  Observation observation;
  metadata.set_customer_id(12345);
  metadata.set_project_id(23456);
  metadata.set_metric_id(34567);
  metadata.set_day_index(45678);
  // Set the random id to
  // 0000000100000001000000010000000100000001000000010000000100000001
  // which is 72340172838076673 in decimal.
  observation.set_random_id(std::string(8, 1));
  std::string row_key = GenerateNewRowKey(metadata, observation);
  EXPECT_EQ(75u, row_key.size());
  // 1329713394 is the hash value we observed for the above-constructed
  // Observation.
  EXPECT_EQ(
      "0000012345:0000023456:0000034567:0000045678:00072340172838076673:"
      "1329713394",
      row_key);

  // Set the random id to a string that is too long. In this case the server
  // will use the first 8 bytes.
  observation.set_random_id(std::string(10, 1));
  row_key = GenerateNewRowKey(metadata, observation);
  EXPECT_EQ(75u, row_key.size());
  // 1577527722 is the hash value we observed for the above-constructed
  // Observation.
  EXPECT_EQ(
      "0000012345:0000023456:0000034567:0000045678:00072340172838076673:"
      "1577527722",
      row_key);

  // Set random_id to a string that is too short. In this case the server
  // generates a random id.
  observation.set_random_id(std::string(2, 1));
  // Generate another row key.
  row_key = GenerateNewRowKey(metadata, observation);
  EXPECT_EQ(75u, row_key.size());
  EXPECT_EQ("0000012345:0000023456:0000034567:0000045678:",
            row_key.substr(0, 44));
  // This is just a sanity check that the random_id part of the row key
  // is not all zeroes.
  EXPECT_NE(":00000000000000000000:", row_key.substr(43, 22));
  // 2704129519 is the hash value we observed for the above-constructed
  // Observation.
  EXPECT_EQ(":2704129519", row_key.substr(64));

  // Clear random_id.
  observation.clear_random_id();
  // Generate another row key.
  row_key = GenerateNewRowKey(metadata, observation);
  EXPECT_EQ(75u, row_key.size());
  EXPECT_EQ("0000012345:0000023456:0000034567:0000045678:",
            row_key.substr(0, 44));
  // 3640671349 is the hash value we observed for the above-constructed
  // Observation.
  EXPECT_EQ(":3640671349", row_key.substr(64));
}

}  // namespace internal

// Now we instantiate ObservationStoreAbstractTest using the MemoryStore
// as the underlying DataStore.

INSTANTIATE_TYPED_TEST_CASE_P(ObservationStoreTest,
                              ObservationStoreAbstractTest, MemoryStoreFactory);

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
