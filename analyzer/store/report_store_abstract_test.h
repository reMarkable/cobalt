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

#ifndef COBALT_ANALYZER_STORE_REPORT_STORE_ABSTRACT_TEST_H_
#define COBALT_ANALYZER_STORE_REPORT_STORE_ABSTRACT_TEST_H_

#include "analyzer/store/report_store.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/store/observation_store_internal.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

// This file contains type-parameterized tests of the ReportStore.
//
// We use C++ templates along with the macros TYPED_TEST_CASE_P and
// TYPED_TEST_P in order to define test templates that may be instantiated to
// to produce concrete tests that use various implementations of Datastore.
//
// See report_store_test.cc and report_store_emulator_test.cc for the
// concrete instantiations.
//
// NOTE: If you add a new test to this file you must add its name to the
// invocation REGISTER_TYPED_TEST_CASE_P macro at the bottom of this file.

namespace cobalt {
namespace analyzer {
namespace store {

// Test value to use for the std_error field. We choose a power 2 so it can
// be represented exactly.
const float kStandardError = 0.25;

// ReportStoreAbstractTest is templatized on the parameter
// |StoreFactoryClass| which must be the name of a class that contains the
// following method: static DataStore* NewStore()
// See MemoryStoreFactory in memory_store_test.cc and
// BigtableStoreEmulatorFactory in bigtable_store_emulator_test.cc.
template <class StoreFactoryClass>
class ReportStoreAbstractTest : public ::testing::Test {
 protected:
  ReportStoreAbstractTest()
      : data_store_(StoreFactoryClass::NewStore()),
        report_store_(new ReportStore(data_store_)) {}

  void SetUp() {
    EXPECT_EQ(kOK, data_store_->DeleteAllRows(DataStore::kReportMetadata));
    EXPECT_EQ(kOK, data_store_->DeleteAllRows(DataStore::kReportRows));
  }

  static const uint32_t kCustomerId = 11;
  static const uint32_t kProjectId = 222;
  static const uint32_t kReportConfigId = 3333;
  static const uint32_t kFirstDayIndex = 12345;
  static const uint32_t kLastDayIndex = 12347;

  static ReportId MakeReportId(uint64_t start_timestamp_ms,
                               uint32_t instance_id) {
    ReportId report_id;
    report_id.set_customer_id(kCustomerId);
    report_id.set_project_id(kProjectId);
    report_id.set_report_config_id(kReportConfigId);
    report_id.set_start_timestamp_ms(start_timestamp_ms);
    report_id.set_instance_id(instance_id);
    return report_id;
  }

  static std::string MakeStringValue(const ReportId& report_id,
                                     size_t row_index, uint8_t variable_index) {
    std::ostringstream stream;
    stream << report_id.start_timestamp_ms() << ":" << report_id.instance_id()
           << ":" << report_id.variable_slice() << ":" << row_index << ":"
           << variable_index;
    return stream.str();
  }

  static void FillValuePart(const ReportId& report_id, size_t row_index,
                            uint8_t variable_index, ValuePart* value_part) {
    value_part->set_string_value(
        MakeStringValue(report_id, row_index, variable_index));
  }

  static void CheckValue(const ValuePart& value_part, size_t row_index,
                         const ReportId& report_id, uint8_t variable_index) {
    EXPECT_EQ(MakeStringValue(report_id, row_index, variable_index),
              value_part.string_value());
  }

  static ReportRow MakeReportRow(const ReportId& report_id, size_t row_index) {
    ReportRow report_row;
    report_row.set_count_estimate(row_index);
    report_row.set_std_error(kStandardError);
    switch (report_id.variable_slice()) {
      case VARIABLE_1:
        FillValuePart(report_id, row_index, 1, report_row.mutable_value());
        break;
      case VARIABLE_2:
        FillValuePart(report_id, row_index, 2, report_row.mutable_value2());
        break;
      case JOINT:
        FillValuePart(report_id, row_index, 1, report_row.mutable_value());
        FillValuePart(report_id, row_index, 2, report_row.mutable_value2());
        break;
      default:
        EXPECT_TRUE(false) << "missing case";
    }
    return report_row;
  }

  static void CheckReportRow(const ReportRow& row, const ReportId& report_id) {
    EXPECT_EQ(kStandardError, row.std_error());
    switch (report_id.variable_slice()) {
      case VARIABLE_1:
        EXPECT_TRUE(row.has_value());
        EXPECT_FALSE(row.has_value2());
        // Note we use the fact that the count_estimate was set to the
        // row_index.
        CheckValue(row.value(), row.count_estimate(), report_id, 1);
        break;
      case VARIABLE_2:
        EXPECT_FALSE(row.has_value());
        EXPECT_TRUE(row.has_value2());
        CheckValue(row.value2(), row.count_estimate(), report_id, 2);
        break;
      case JOINT:
        EXPECT_TRUE(row.has_value());
        EXPECT_TRUE(row.has_value2());
        CheckValue(row.value(), row.count_estimate(), report_id, 1);
        CheckValue(row.value2(), row.count_estimate(), report_id, 2);
        break;
      default:
        EXPECT_TRUE(false) << "missing case";
    }
  }

  std::string ToString(const ReportId& report_id) {
    return report_store_->MakeMetadataRowKey(report_id);
  }

  Status StartNewReport(bool requested, ReportId* report_id) {
    return this->report_store_->StartNewReport(kFirstDayIndex, kLastDayIndex,
                                               requested, report_id);
  }

  // Starts a new report with requested=true and our global contstant values
  // for all of the parameters.
  ReportId StartNewReport() {
    bool requested = true;

    // Make a new ReportID without specifying timestamp or instance_id.
    ReportId report_id = MakeReportId(0, 0);
    EXPECT_EQ(0, report_id.start_timestamp_ms());
    EXPECT_EQ(0, report_id.instance_id());

    // Invoke StartNewReport().
    EXPECT_EQ(kOK, StartNewReport(requested, &report_id));
    return report_id;
  }

  void BulkWriteMetadata(const std::vector<ReportId>& report_ids,
                         const std::vector<ReportMetadata>& metadata) {
    EXPECT_EQ(report_ids.size(), metadata.size());
    std::vector<DataStore::Row> rows;
    for (int i = 0; i < report_ids.size(); i++) {
      rows.emplace_back(
          report_store_->MakeDataStoreRow(report_ids[i], metadata[i]));
    }

    EXPECT_EQ(kOK, data_store_->WriteRows(DataStore::kReportMetadata,
                                          std::move(rows)));
  }

  // Inserts |num_timestamps| * 6 rows into the report_metadata table.
  // Starting with timestamp=start_timestamp, for |num_timestamps| increments of
  // |timestamp_delta|, 6 rows are inserted with that timestamp: For each of
  // the 3 variable slices we insert two rows with two different values of
  // instance_id. For each insert we store timestamp + instance_id + slice_index
  // into the ReportMetadata's start_timestamp_ms field for later verifaction.
  void WriteManyNewReports(uint64_t start_timestamp, uint64_t timestamp_delta,
                           size_t num_timestamps) {
    std::vector<ReportId> report_ids;
    std::vector<ReportMetadata> metadata_vector;
    uint64_t timestamp = start_timestamp;
    for (int ts_index = 0; ts_index < num_timestamps; ts_index++) {
      for (int instance_id = 0; instance_id <= 1; instance_id++) {
        for (int slice_index = 0; slice_index < 3; slice_index++) {
          report_ids.emplace_back(MakeReportId(timestamp, instance_id));
          EXPECT_EQ(report_ids.back().instance_id(), instance_id);
          report_ids.back().set_variable_slice((VariableSlice)slice_index);
          ReportMetadata metadata;
          metadata.set_start_timestamp_ms(timestamp + instance_id +
                                          slice_index);
          metadata_vector.emplace_back(metadata);
        }
      }
      timestamp += timestamp_delta;
    }
    BulkWriteMetadata(report_ids, metadata_vector);
  }

  void AddReportRows(const ReportId& report_id, size_t num_rows) {
    std::vector<ReportRow> report_rows;
    for (int index = 0; index < num_rows; index++) {
      report_rows.emplace_back(MakeReportRow(report_id, index));
    }
    EXPECT_EQ(kOK, report_store_->AddReportRows(report_id, report_rows));
  }

  void GetReportAndCheck(const ReportId& report_id, size_t expected_num_rows) {
    Report report;
    EXPECT_EQ(kOK, report_store_->GetReport(report_id, &report));
    const ReportMetadata& read_metadata = report.metadata();
    EXPECT_EQ(ReportMetadata::COMPLETED_SUCCESSFULLY, read_metadata.status());
    EXPECT_EQ(expected_num_rows, report.rows_size());
    for (const auto& row : report.rows()) {
      this->CheckReportRow(row, report_id);
    }
  }

  uint32_t first_day_index() const { return kFirstDayIndex; }

  uint32_t last_day_index() const { return kLastDayIndex; }

  uint32_t customer_id() const { return kCustomerId; }

  uint32_t project_id() const { return kProjectId; }

  uint32_t report_config_id() const { return kReportConfigId; }

  std::shared_ptr<DataStore> data_store_;
  std::unique_ptr<ReportStore> report_store_;
};

TYPED_TEST_CASE_P(ReportStoreAbstractTest);

// Tests the methods StartNewReport(), EndReport() and GetMetadata().
TYPED_TEST_P(ReportStoreAbstractTest, SetAndGetMetadata) {
  bool requested = true;

  // Make a new ReportID without specifying timestamp or instance_id.
  ReportId report_id = this->MakeReportId(0, 0);
  EXPECT_EQ(0, report_id.start_timestamp_ms());
  EXPECT_EQ(0, report_id.instance_id());

  // Invoke StartNewReport().
  EXPECT_EQ(kOK, this->StartNewReport(requested, &report_id));

  // Check that the report_id was completed.
  EXPECT_NE(0, report_id.start_timestamp_ms());
  EXPECT_NE(0, report_id.instance_id());

  // Get the ReportMetatdata for this new ID.
  ReportMetadata report_metadata;
  EXPECT_EQ(kOK, this->report_store_->GetMetadata(report_id, &report_metadata));

  // Check its state.
  EXPECT_EQ(ReportMetadata::IN_PROGRESS, report_metadata.status());
  EXPECT_EQ(this->first_day_index(), report_metadata.first_day_index());
  EXPECT_EQ(this->last_day_index(), report_metadata.last_day_index());
  EXPECT_EQ(requested, report_metadata.requested());
  EXPECT_EQ(report_id.start_timestamp_ms(),
            report_metadata.start_timestamp_ms());
  EXPECT_EQ(0, report_metadata.finish_timestamp_ms());
  EXPECT_EQ("", report_metadata.info_message());

  // Invoke EndReport() with success=true.
  bool success = true;
  EXPECT_EQ(kOK, this->report_store_->EndReport(report_id, success, "hello"));

  // Get the ReportMetatdata again.
  report_metadata.Clear();
  EXPECT_EQ(kOK, this->report_store_->GetMetadata(report_id, &report_metadata));

  // Check its state. It should now be completed and have a finish_timestamp.
  EXPECT_EQ(ReportMetadata::COMPLETED_SUCCESSFULLY, report_metadata.status());
  EXPECT_EQ(this->first_day_index(), report_metadata.first_day_index());
  EXPECT_EQ(this->last_day_index(), report_metadata.last_day_index());
  EXPECT_EQ(requested, report_metadata.requested());
  EXPECT_EQ(report_id.start_timestamp_ms(),
            report_metadata.start_timestamp_ms());
  EXPECT_NE(0, report_metadata.finish_timestamp_ms());
  EXPECT_EQ("hello", report_metadata.info_message());

  // Invoke EndReport() with success=false. Note that we never do this in
  // the real product (i.e. convert from COMPLETED_SUCCESSFULLY to
  // TERMINATED) but it is a convenient shortcut for the test.
  success = false;
  EXPECT_EQ(kOK, this->report_store_->EndReport(report_id, success, "goodbye"));

  // Get the ReportMetatdata again.
  report_metadata.Clear();
  EXPECT_EQ(kOK, this->report_store_->GetMetadata(report_id, &report_metadata));

  // Check its state. It should now be terminated.
  EXPECT_EQ(ReportMetadata::TERMINATED, report_metadata.status());
  EXPECT_EQ("hello : goodbye", report_metadata.info_message());
}

// Tests the function StartAssociatedSlice().
TYPED_TEST_P(ReportStoreAbstractTest, StartAssociatedSlice) {
  bool requested = false;

  // Make a new ReportID without specifying timestamp or instance_id.
  ReportId report_id1 = this->MakeReportId(0, 0);
  EXPECT_EQ(VARIABLE_1, report_id1.variable_slice());

  // Invoke StartNewReport().
  EXPECT_EQ(kOK, this->StartNewReport(requested, &report_id1));

  // Invoke EndReport()
  EXPECT_EQ(kOK, this->report_store_->EndReport(report_id1, true, "hello"));

  // Copy the new report_id
  ReportId report_id2(report_id1);

  // Invoke StartAssociatedSlice().
  EXPECT_EQ(kOK,
            this->report_store_->StartAssociatedSlice(VARIABLE_2, &report_id2));

  // Check that report_id2 had its variable_slice set correctly.
  EXPECT_EQ(VARIABLE_2, report_id2.variable_slice());

  // Get the ReportMetatdata for report_id2.
  ReportMetadata report_metadata;
  EXPECT_EQ(kOK,
            this->report_store_->GetMetadata(report_id2, &report_metadata));

  // Check its state.
  EXPECT_EQ(ReportMetadata::IN_PROGRESS, report_metadata.status());
  EXPECT_EQ(this->first_day_index(), report_metadata.first_day_index());
  EXPECT_EQ(this->last_day_index(), report_metadata.last_day_index());
  EXPECT_EQ(requested, report_metadata.requested());
  EXPECT_NE(0, report_metadata.start_timestamp_ms());

  // finish_timestamp and info_message should not have been copied to this
  // ReportMetadata.
  EXPECT_EQ(0, report_metadata.finish_timestamp_ms());
  EXPECT_EQ("", report_metadata.info_message());
}

// Tests the functions AddReportRow and GetReport
TYPED_TEST_P(ReportStoreAbstractTest, ReportRows) {
  // We start three reports. Two independent reports, report 1 and report 2.
  auto report_id1 = this->StartNewReport();
  auto report_id2 = this->StartNewReport();
  // And report 2a which is an associated sub-reprot with report 2.
  ReportId report_id2a(report_id2);
  EXPECT_EQ(
      kOK, this->report_store_->StartAssociatedSlice(VARIABLE_2, &report_id2a));

  // Add rows to all three reports.
  this->AddReportRows(report_id1, 100);
  this->AddReportRows(report_id2, 200);
  this->AddReportRows(report_id2a, 300);

  // Complete all three reports
  this->report_store_->EndReport(report_id1, true, "");
  this->report_store_->EndReport(report_id2, true, "");
  this->report_store_->EndReport(report_id2a, true, "");

  // Fetch report 1 and check it.
  this->GetReportAndCheck(report_id1, 100);

  // Fetch report 2 and check it.
  this->GetReportAndCheck(report_id2, 200);

  // Fetch report 2a and check it.
  this->GetReportAndCheck(report_id2a, 300);
}

// Tests the function QueryReports
TYPED_TEST_P(ReportStoreAbstractTest, QueryReports) {
  static const uint64_t kStartTimestamp = 123456789;
  static const uint64_t kTimestampDelta = 10;
  size_t num_timestamps = 50;

  // According to the comments on WriteManyNewReports, we are inserting
  // 6*50 = 300 new report rows: 6 for each of the 50 timestamps specified
  // by kStartTimestamp and kTimestampDelta;
  this->WriteManyNewReports(kStartTimestamp, kTimestampDelta, num_timestamps);

  // Query for 120 of the  300 rows.
  uint64_t first_start_timestamp_ms = kStartTimestamp + 5 * kTimestampDelta;
  uint64_t limit_start_timestamp_ms = kStartTimestamp + 25 * kTimestampDelta;
  auto query_reports_response = this->report_store_->QueryReports(
      this->customer_id(), this->project_id(), this->report_config_id(),
      first_start_timestamp_ms, limit_start_timestamp_ms, INT_MAX, "");

  // Check the results.
  ASSERT_EQ(kOK, query_reports_response.status);
  EXPECT_TRUE(query_reports_response.pagination_token.empty());
  ASSERT_EQ(120, query_reports_response.results.size());
  for (const auto& report_record : query_reports_response.results) {
    const ReportId& report_id = report_record.report_id;
    EXPECT_EQ(this->customer_id(), report_id.customer_id());
    EXPECT_EQ(this->project_id(), report_id.project_id());
    EXPECT_EQ(this->report_config_id(), report_id.report_config_id());
    uint64_t timestamp = report_id.start_timestamp_ms();
    EXPECT_TRUE(first_start_timestamp_ms <= timestamp);
    EXPECT_TRUE(timestamp < limit_start_timestamp_ms);
    // See WriteManyNewReports for how we set
    // report_metadata.start_timestamp_ms()
    EXPECT_EQ(timestamp + report_id.instance_id() + report_id.variable_slice(),
              report_record.report_metadata.start_timestamp_ms());
  }

  // Query again. This time we set limit_start_timestamp = infinity and
  // we query the results in batches of 100.
  std::vector<ReportStore::ReportRecord> full_results;
  first_start_timestamp_ms = kStartTimestamp + 5 * kTimestampDelta;
  limit_start_timestamp_ms = UINT64_MAX;
  std::string pagination_token = "";
  do {
    query_reports_response = this->report_store_->QueryReports(
        this->customer_id(), this->project_id(), this->report_config_id(),
        first_start_timestamp_ms, limit_start_timestamp_ms, 100,
        pagination_token);
    EXPECT_EQ(kOK, query_reports_response.status);
    for (auto& result : query_reports_response.results) {
      full_results.emplace_back(std::move(result));
    }
    pagination_token = std::move(query_reports_response.pagination_token);
  } while (!pagination_token.empty());

  // Check the results.
  ASSERT_EQ(270, full_results.size());
  for (const auto& report_record : full_results) {
    const ReportId& report_id = report_record.report_id;
    EXPECT_EQ(this->customer_id(), report_id.customer_id());
    EXPECT_EQ(this->project_id(), report_id.project_id());
    EXPECT_EQ(this->report_config_id(), report_id.report_config_id());
    uint64_t timestamp = report_id.start_timestamp_ms();
    EXPECT_TRUE(first_start_timestamp_ms <= timestamp);
    EXPECT_TRUE(timestamp < limit_start_timestamp_ms);
    // See WriteManyNewReports for how we set
    // report_metadata.start_timestamp_ms()
    EXPECT_EQ(timestamp + report_id.instance_id() + report_id.variable_slice(),
              report_record.report_metadata.start_timestamp_ms());
  }
}

REGISTER_TYPED_TEST_CASE_P(ReportStoreAbstractTest, SetAndGetMetadata,
                           StartAssociatedSlice, ReportRows, QueryReports);

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_REPORT_STORE_ABSTRACT_TEST_H_
