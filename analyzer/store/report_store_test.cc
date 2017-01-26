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

#include "analyzer/store/report_store.h"

#include <string>
#include <utility>

#include "analyzer/store/memory_store_test_helper.h"
#include "analyzer/store/report_store_abstract_test.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {
namespace store {

namespace {

const uint32_t kCustomerId = 11;
const uint32_t kProjectId = 222;
const uint32_t kReportConfigId = 3333;

ReportId MakeReportId(uint64_t start_timestamp_ms, uint32_t instance_id) {
  ReportId report_id;
  report_id.set_customer_id(kCustomerId);
  report_id.set_project_id(kProjectId);
  report_id.set_report_config_id(kReportConfigId);
  report_id.set_start_timestamp_ms(start_timestamp_ms);
  report_id.set_instance_id(instance_id);
  return report_id;
}

}  // namespace

// Tests of the private static functions of ReportStore. These do not involve
// a DataStore and so they are included only here in this concrete test and
// not in ReportStoreAbstractTest.
class ReportStorePrivateTest : public ::testing::Test {
 protected:
  static std::string MakeMetadataRowKey(const ReportId& report_id) {
    return ReportStore::MakeMetadataRowKey(report_id);
  }

  static std::string MetadataRangeStartKey(uint64_t start_timestamp_millis) {
    return ReportStore::MetadataRangeStartKey(
        kCustomerId, kProjectId, kReportConfigId, start_timestamp_millis);
  }

  static std::string ReportStartRowKey(const ReportId& report_id) {
    return ReportStore::ReportStartRowKey(report_id);
  }

  static std::string ReportEndRowKey(const ReportId& report_id) {
    return ReportStore::ReportEndRowKey(report_id);
  }

  static std::string GenerateReportRowKey(const ReportId& report_id) {
    return ReportStore::GenerateReportRowKey(report_id);
  }
};

TEST_F(ReportStorePrivateTest, MakeMetadataRowKeyTest) {
  ReportId report_id = MakeReportId(12345, 54321);
  EXPECT_EQ(
      "0000000011:0000000222:0000003333:00000000000000012345:0000054321:0",
      MakeMetadataRowKey(report_id));

  report_id.set_variable_slice(VARIABLE_1);
  EXPECT_EQ(
      "0000000011:0000000222:0000003333:00000000000000012345:0000054321:0",
      MakeMetadataRowKey(report_id));

  report_id.set_variable_slice(VARIABLE_2);
  EXPECT_EQ(
      "0000000011:0000000222:0000003333:00000000000000012345:0000054321:1",
      MakeMetadataRowKey(report_id));

  report_id.set_variable_slice(JOINT);
  EXPECT_EQ(
      "0000000011:0000000222:0000003333:00000000000000012345:0000054321:2",
      MakeMetadataRowKey(report_id));
}

TEST_F(ReportStorePrivateTest, MetadataRangeStartKeyTest) {
  EXPECT_EQ(
      "0000000011:0000000222:0000003333:00000000000000123456:0000000000:0",
      MetadataRangeStartKey(123456));
}

TEST_F(ReportStorePrivateTest, ReportStartRowKeyTest) {
  ReportId report_id = MakeReportId(12345, 54321);
  EXPECT_EQ(
      "0000000011:0000000222:0000003333:00000000000000012345:0000054321:0:",
      ReportStartRowKey(report_id));
}

TEST_F(ReportStorePrivateTest, ReportEndRowKeyTest) {
  ReportId report_id = MakeReportId(12345, 54321);
  EXPECT_EQ(
      "0000000011:0000000222:0000003333:00000000000000012345:0000054321:0:"
      "9999999999",
      ReportEndRowKey(report_id));
}

TEST_F(ReportStorePrivateTest, GenerateReportRowKeyTest) {
  ReportId report_id = MakeReportId(12345, 54321);
  std::string generated_report_row_key = GenerateReportRowKey(report_id);
  EXPECT_TRUE(ReportStartRowKey(report_id) < generated_report_row_key);
  EXPECT_TRUE(ReportEndRowKey(report_id) > generated_report_row_key);
}

// Now we instantiate ReportStoreAbstractTest using the MemoryStore
// as the underlying DataStore.

INSTANTIATE_TYPED_TEST_CASE_P(ReportStoreTest, ReportStoreAbstractTest,
                              MemoryStoreFactory);

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
