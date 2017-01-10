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

#include "analyzer/store/memory_store.h"
#include "analyzer/store/observation_store_internal.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {
namespace store {

namespace {

static const uint32_t kCustomerId = 1;
static const uint32_t kProjectId = 1;

// Generates a part name with the given index.
std::string PartName(int index) {
  std::string out(15, 0);
  std::snprintf(&out[0], out.size(), "part%.10d", index);
  return out;
}

}  // namespace

// Tests of the internal functions.
namespace internal {

// Tests the functions RowKey() and DayIndexFromRowKey().
TEST(ObservationStoreInteralTest, DayIndexFromRowKey) {
  std::string row_key = RowKey(39, 40, 41, 42, 43, 44);
  EXPECT_EQ(
      "0000000039:0000000040:0000000041:0000000042:00000000000000000043:"
      "0000000044",
      row_key);
  EXPECT_EQ(42, DayIndexFromRowKey(row_key));
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
  std::string row_key = GenerateNewRowKey(12345, 23456, 34567, 45678);
  // Check that row_key has the right length.
  EXPECT_EQ(75, row_key.size());
  // Check that the last two fields are not identically zero.
  EXPECT_NE(
      "0000012345:0000023456:0000034567:0000045678:00000000000000000000:"
      "0000000000",
      row_key);
  // Check all but the last two fields. We don't check the time field or
  // the random field.
  row_key.resize(44);
  EXPECT_EQ("0000012345:0000023456:0000034567:0000045678:", row_key);
}

}  // namespace internal

class ObservationStoreTest : public ::testing::Test {
 protected:
  ObservationStoreTest()
      : mem_store_(new MemoryStore()),
        observation_store_(new ObservationStore(mem_store_)) {
    mem_store_->DeleteAllRows(DataStore::kObservations);
  }

  void AddObservation(uint32_t metric_id, uint32_t day_index, int num_parts) {
    ObservationMetadata metadata;
    metadata.set_customer_id(kCustomerId);
    metadata.set_project_id(kProjectId);
    metadata.set_metric_id(metric_id);
    metadata.set_day_index(day_index);
    Observation observation;
    for (int part_index = 0; part_index < num_parts; part_index++) {
      std::string part_name = PartName(part_index);
      ObservationPart observation_part;
      switch (part_index % 3) {
        case 0:
          observation_part.mutable_forculus()->set_ciphertext(part_name);
          break;
        case 1:
          observation_part.mutable_rappor()->set_data(part_name);
          break;
        default:
          observation_part.mutable_basic_rappor()->set_data(part_name);
          break;
      }
      (*observation.mutable_parts())[part_name].Swap(&observation_part);
    }
    EXPECT_EQ(kOK, observation_store_->AddObservation(metadata, observation));
  }

  void AddObservations(uint32_t metric_id, uint32_t first_day_index,
                       int32_t last_day_index, int num_per_day, int num_parts) {
    for (uint32_t day_index = first_day_index; day_index <= last_day_index;
         day_index++) {
      for (int i = 0; i < num_per_day; i++) {
        AddObservation(metric_id, day_index, num_parts);
      }
    }
  }

  // Repeatedly invokes QueryObservations using the given data until all of
  // the results have been obtained. Returns the full list of results.
  std::vector<ObservationStore::QueryResult> QueryFullResults(
      uint32_t metric_id, uint32_t first_day_index, int32_t last_day_index,
      int num_parts, size_t max_results) {
    std::vector<std::string> parts;
    for (int part_index = 0; part_index < num_parts; part_index++) {
      parts.push_back(PartName(part_index));
    }
    std::vector<ObservationStore::QueryResult> full_results;
    std::string pagination_token = "";
    do {
      ObservationStore::QueryResponse query_response =
          observation_store_->QueryObservations(
              kCustomerId, kProjectId, metric_id, first_day_index,
              last_day_index, parts, max_results, pagination_token);
      EXPECT_EQ(kOK, query_response.status);
      for (auto& result : query_response.results) {
        full_results.emplace_back(std::move(result));
      }
      pagination_token = std::move(query_response.pagination_token);
    } while (!pagination_token.empty());
    return full_results;
  }

  std::shared_ptr<MemoryStore> mem_store_;
  std::unique_ptr<ObservationStore> observation_store_;
};

void CheckFullResults(
    const std::vector<ObservationStore::QueryResult>& full_results,
    size_t expected_num_results, size_t expected_num_results_per_day,
    size_t expected_num_parts, uint32_t expected_first_day_index) {
  EXPECT_EQ(expected_num_results, full_results.size());
  int result_index = 0;
  uint32_t expected_day_index = expected_first_day_index;
  for (const auto& result : full_results) {
    EXPECT_EQ(expected_day_index, result.day_index);
    EXPECT_EQ(expected_num_parts, result.observation.parts().size());
    for (int part_index = 0; part_index < expected_num_parts; part_index++) {
      std::string expected_part_name = PartName(part_index);
      switch (part_index % 3) {
        case 0:
          EXPECT_TRUE(
              result.observation.parts().at(expected_part_name).has_forculus());
          break;
        case 1:
          EXPECT_TRUE(
              result.observation.parts().at(expected_part_name).has_rappor());
          break;
        default:
          EXPECT_TRUE(result.observation.parts()
                          .at(expected_part_name)
                          .has_basic_rappor());
          break;
      }
    }
    if (++result_index == expected_num_results_per_day) {
      result_index = 0;
      expected_day_index++;
    }
  }
}

TEST_F(ObservationStoreTest, AddAndQuery) {
  // For metric 1, add 100 observations with 2 parts each for each day in the
  // range [100, 109].
  uint32_t metric_id = 1;
  AddObservations(metric_id, 100, 109, 100, 2);

  // For metric 2, add 200 observations with 1 part each for each day in the
  // range [101, 110].
  metric_id = 2;
  AddObservations(metric_id, 101, 110, 200, 1);

  /////////////////////////////////////////////////////////////////
  // Queries for metric 1
  /////////////////////////////////////////////////////////////////
  metric_id = 1;

  // Query for observations for days in the range [50, 150].
  // Ask for 2 parts.
  // Don't impose a maximum number of results.
  int requested_num_parts = 2;
  std::vector<ObservationStore::QueryResult> full_results =
      QueryFullResults(metric_id, 50, 150, requested_num_parts, 0);

  // Expect to find 1000 results as 100 results per day for 10 days starting
  // with day 100.
  // Expect to find 2 parts.
  int expected_num_results = 1000;
  int expected_num_results_per_day = 100;
  int expected_first_day_index = 100;
  int expected_num_parts = 2;
  CheckFullResults(full_results, expected_num_results,
                   expected_num_results_per_day, expected_num_parts,
                   expected_first_day_index);

  //------------------------------------------------------------

  // Query for observations for days in the range [0, UINT32_MAX].
  full_results =
      QueryFullResults(metric_id, 0, UINT32_MAX, requested_num_parts, 0);

  // Expect the same results as above.
  CheckFullResults(full_results, expected_num_results,
                   expected_num_results_per_day, expected_num_parts,
                   expected_first_day_index);

  //------------------------------------------------------------

  // Query for observations for days in the range [100, 105].
  full_results = QueryFullResults(metric_id, 100, 105, requested_num_parts, 0);

  // Expect to find 600 results as 100 results per day for 6 days.
  expected_num_results = 600;
  CheckFullResults(full_results, expected_num_results,
                   expected_num_results_per_day, expected_num_parts,
                   expected_first_day_index);

  //------------------------------------------------------------

  // Query for observations for days in the range [105, 110].
  full_results = QueryFullResults(metric_id, 105, 110, requested_num_parts, 0);

  // Expect to find 500 results as 100 results per day for 5 days starting
  // with day 105.
  expected_num_results = 500;
  expected_first_day_index = 105;
  CheckFullResults(full_results, expected_num_results,
                   expected_num_results_per_day, expected_num_parts,
                   expected_first_day_index);

  //------------------------------------------------------------

  // Test that it works to not specify any requested parts. We should get
  // all of the parts.
  requested_num_parts = 0;

  // Query for observations for days in the range [105, 110].
  full_results = QueryFullResults(metric_id, 105, 110, requested_num_parts, 0);

  // Expect to find 500 results as 100 results per day for 5 days starting
  // with day 105.
  expected_num_results = 500;
  expected_first_day_index = 105;
  CheckFullResults(full_results, expected_num_results,
                   expected_num_results_per_day, expected_num_parts,
                   expected_first_day_index);

  //------------------------------------------------------------

  // Test that it works to request 1 part when there are two. We should
  // receive only 1.
  requested_num_parts = 1;
  expected_num_parts = 1;

  // Query for observations for days in the range [105, 110].
  full_results = QueryFullResults(metric_id, 105, 110, requested_num_parts, 0);

  // Expect to find 500 results as 100 results per day for 5 days starting
  // with day 105.
  expected_num_results = 500;
  expected_first_day_index = 105;
  CheckFullResults(full_results, expected_num_results,
                   expected_num_results_per_day, expected_num_parts,
                   expected_first_day_index);

  /////////////////////////////////////////////////////////////////
  // Queries for metric 2
  /////////////////////////////////////////////////////////////////
  metric_id = 2;

  // Query for observations for days in the range [50, 150].
  full_results = QueryFullResults(metric_id, 50, 150, requested_num_parts, 0);

  // Expect to find 2000 results as 200 results per day for 10 days starting
  // with day 101.
  // Expect to find 1 part.
  expected_num_results = 2000;
  expected_num_results_per_day = 200;
  expected_num_parts = 1;
  expected_first_day_index = 101;
  CheckFullResults(full_results, expected_num_results,
                   expected_num_results_per_day, expected_num_parts,
                   expected_first_day_index);

  //------------------------------------------------------------

  // Query for observations for days in the range [0, UINT32_MAX].
  full_results =
      QueryFullResults(metric_id, 0, UINT32_MAX, requested_num_parts, 0);

  // Expect the same results as above.
  CheckFullResults(full_results, expected_num_results,
                   expected_num_results_per_day, expected_num_parts,
                   expected_first_day_index);

  //------------------------------------------------------------

  // Query for observations for days in the range [100, 105].
  full_results = QueryFullResults(metric_id, 100, 105, requested_num_parts, 0);

  // Expect to find 1000 results as 200 results per day for 5 days.
  expected_num_results = 1000;
  CheckFullResults(full_results, expected_num_results,
                   expected_num_results_per_day, expected_num_parts,
                   expected_first_day_index);

  //------------------------------------------------------------

  // Query for observations for days in the range [105, 110].
  full_results = QueryFullResults(metric_id, 105, 110, requested_num_parts, 0);

  // Expect to find 1200 results as 200 results per day for 6 days starting
  // with day 105.
  expected_num_results = 1200;
  expected_first_day_index = 105;
  CheckFullResults(full_results, expected_num_results,
                   expected_num_results_per_day, expected_num_parts,
                   expected_first_day_index);

  //------------------------------------------------------------

  // Test that it works to not specify any requested parts. We should get
  // all of the parts.
  requested_num_parts = 0;

  // Query for observations for days in the range [105, 110].
  full_results = QueryFullResults(metric_id, 105, 110, requested_num_parts, 0);

  // Expect to find 1200 results as 200 results per day for 6 days starting
  // with day 105.
  expected_num_results = 1200;
  expected_first_day_index = 105;
  CheckFullResults(full_results, expected_num_results,
                   expected_num_results_per_day, expected_num_parts,
                   expected_first_day_index);

  //------------------------------------------------------------

  // Test that it works to request 1 part when there is one part.
  requested_num_parts = 1;

  // Query for observations for days in the range [105, 110].
  full_results = QueryFullResults(metric_id, 105, 110, requested_num_parts, 0);

  // Expect to find 1200 results as 200 results per day for 6 days starting
  // with day 105.
  expected_num_results = 1200;
  expected_first_day_index = 105;
  CheckFullResults(full_results, expected_num_results,
                   expected_num_results_per_day, expected_num_parts,
                   expected_first_day_index);

  /////////////////////////////////////////////////////////////////
  // Queries for metric 3
  /////////////////////////////////////////////////////////////////

  // For metric 3 expect to find 0 results.
  metric_id = 3;
  full_results =
      QueryFullResults(metric_id, 0, UINT32_MAX, requested_num_parts, 0);
  EXPECT_EQ(0, full_results.size());

  /////////////////////////////////////////////////////////////////
  // Queries for metric 0
  /////////////////////////////////////////////////////////////////

  // For metric 0 expect to find 0 results.
  metric_id = 0;
  full_results =
      QueryFullResults(metric_id, 0, UINT32_MAX, requested_num_parts, 0);
  EXPECT_EQ(0, full_results.size());
}

TEST_F(ObservationStoreTest, QueryWithInvalidArguments) {
  uint32_t metric_id = 1;
  uint32_t first_day_index = 42;
  uint32_t last_day_index = 42;

  // Try to use a pagination token that corresponds to a day index that
  // is too small. Expect kInvalidArguments.
  std::string pagination_token = internal::GenerateNewRowKey(
      kCustomerId, kProjectId, metric_id, first_day_index - 1);

  std::vector<std::string> parts;
  ObservationStore::QueryResponse query_response =
      observation_store_->QueryObservations(kCustomerId, kProjectId, metric_id,
                                            first_day_index, last_day_index,
                                            parts, 0, pagination_token);

  EXPECT_EQ(kInvalidArguments, query_response.status);

  // Switch to a pagination token that corresponds to first_day_index.
  // Expect kOK.
  pagination_token = internal::GenerateNewRowKey(kCustomerId, kProjectId,
                                                 metric_id, first_day_index);
  query_response = observation_store_->QueryObservations(
      kCustomerId, kProjectId, metric_id, first_day_index, last_day_index,
      parts, 0, pagination_token);
  EXPECT_EQ(kOK, query_response.status);

  // Try to use a last_day_index < first_day_index. Expect kInvalidArguments.
  last_day_index = first_day_index - 1;
  query_response = observation_store_->QueryObservations(
      kCustomerId, kProjectId, metric_id, first_day_index, last_day_index,
      parts, 0, "");
  EXPECT_EQ(kInvalidArguments, query_response.status);

  // Switch to last_day_index = first_day_index. Expect kOK.
  last_day_index = first_day_index;
  query_response = observation_store_->QueryObservations(
      kCustomerId, kProjectId, metric_id, first_day_index, last_day_index,
      parts, 0, "");
  EXPECT_EQ(kOK, query_response.status);
}

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
