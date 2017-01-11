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

// Now we instantiate ObservationStoreAbstractTest using the MemoryStore
// as the underlying DataStore.

INSTANTIATE_TYPED_TEST_CASE_P(ObservationStoreTest,
                              ObservationStoreAbstractTest, MemoryStoreFactory);

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
