// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_serializer.h"

#include <memory>
#include <sstream>

#include "config/report_config.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {

using config::ReportRegistry;

namespace {

const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;
const uint32_t kSomeDayIndex = 123456;

const char* kReportConfigText = R"(
element {
  customer_id: 1
  project_id: 1
  id: 1
  metric_id: 1
  variable {
    metric_part: "Fruit"
  }
  variable {
    metric_part: "City"
  }
  export_configs {
    csv {}
  }
}

element {
  customer_id: 1
  project_id: 1
  id: 2
  metric_id: 1
  # This export_config is invalid.
  export_configs {
  }
}
)";

ReportMetadataLite BuildHistogramMetadata(uint32_t variable_index) {
  ReportMetadataLite metadata;
  metadata.set_report_type(ReportType::HISTOGRAM);
  metadata.add_variable_indices(variable_index);
  metadata.set_first_day_index(kSomeDayIndex);
  metadata.set_last_day_index(kSomeDayIndex);
  return metadata;
}

void AddFloatValues(float count_estimate, float std_error,
                    HistogramReportRow* row) {
  row->set_count_estimate(count_estimate);
  row->set_std_error(std_error);
}

ReportRow HistogramReportIntValueRow(int value, float count_estimate,
                                     float std_error) {
  ReportRow report_row;
  HistogramReportRow* row = report_row.mutable_histogram();
  row->mutable_value()->set_int_value(value);
  AddFloatValues(count_estimate, std_error, row);
  return report_row;
}

ReportRow HistogramReportStringValueRow(const std::string& value,
                                        float count_estimate, float std_error) {
  ReportRow report_row;
  HistogramReportRow* row = report_row.mutable_histogram();
  row->mutable_value()->set_string_value(value);
  AddFloatValues(count_estimate, std_error, row);
  return report_row;
}

ReportRow HistogramReportBlobValueRow(const std::string& value,
                                      float count_estimate, float std_error) {
  ReportRow report_row;
  HistogramReportRow* row = report_row.mutable_histogram();
  row->mutable_value()->set_blob_value(value);
  AddFloatValues(count_estimate, std_error, row);
  return report_row;
}

ReportRow HistogramReportIndexValueRow(int index, const std::string& label,
                                       float count_estimate, float std_error) {
  ReportRow report_row;
  HistogramReportRow* row = report_row.mutable_histogram();
  row->mutable_value()->set_index_value(index);
  row->set_label(label);
  AddFloatValues(count_estimate, std_error, row);
  return report_row;
}

}  // namespace

class ReportSerializerTest : public ::testing::Test {
 public:
  void SetUp() {
    // Parse the report config string
    auto report_parse_result =
        ReportRegistry::FromString(kReportConfigText, nullptr);
    EXPECT_EQ(config::kOK, report_parse_result.second);
    report_registry_.reset((report_parse_result.first.release()));
  }

  grpc::Status SerializeHistogramReport(
      uint32_t report_config_id, uint32_t variable_index,
      const std::vector<ReportRow>& report_rows,
      std::string* serialized_report_out, std::string* mime_type_out) {
    const auto* report_config =
        report_registry_->Get(kCustomerId, kProjectId, report_config_id);
    CHECK(report_config);
    auto metadata = BuildHistogramMetadata(variable_index);
    return SerializeReport(*report_config, metadata, report_rows,
                           serialized_report_out, mime_type_out);
  }

  void DoSerializeHistogramReportTest(
      uint32_t report_config_id, uint32_t variable_index,
      const std::vector<ReportRow>& report_rows,
      const std::string expected_mime_type,
      const std::string& expected_serialization) {
    std::string mime_type;
    std::string report;

    // Test firt using the method SerializeReport().
    auto status = SerializeHistogramReport(report_config_id, variable_index,
                                           report_rows, &report, &mime_type);
    EXPECT_TRUE(status.ok()) << status.error_message() << " ";
    EXPECT_EQ(expected_mime_type, mime_type);
    EXPECT_EQ(expected_serialization, report);

    // Test again using the methods StartSerializingReport() and AppendRows().
    auto metadata = BuildHistogramMetadata(variable_index);
    auto report_config =
        report_registry_->Get(kCustomerId, kProjectId, report_config_id);
    ReportSerializer serializer(report_config, &metadata,
                                &(report_config->export_configs(0)));
    std::ostringstream stream;
    status = serializer.StartSerializingReport(&stream);
    EXPECT_TRUE(status.ok()) << status.error_message() << " ";
    EXPECT_EQ(expected_mime_type, serializer.mime_type());

    // Break the expected serialization into lines
    std::stringstream expected_stream(expected_serialization);
    std::vector<std::string> expected_lines;
    std::string line;
    while (std::getline(expected_stream, line)) {
      expected_lines.push_back(line + "\n");
    }

    // Check that StartSerializingReport() wrote the header line.
    EXPECT_FALSE(expected_lines.empty());
    CHECK_GT(expected_lines.size(), 0);
    EXPECT_EQ(expected_lines[0], stream.str());

    // Test AppendRows() in a mode where we we append a single row at a time.
    // We set max_num_bytes = 1 to ensure that this happens.
    ReportRowVectorIterator row_iterator(&report_rows);
    size_t max_num_bytes = 1;
    for (size_t row_num = 1; row_num < expected_lines.size(); row_num++) {
      std::ostringstream stream;
      status = serializer.AppendRows(max_num_bytes, &row_iterator, &stream);
      EXPECT_TRUE(status.ok()) << status.error_message() << " ";
      EXPECT_GT(expected_lines.size(), row_num);
      CHECK_GT(expected_lines.size(), row_num);
      EXPECT_EQ(expected_lines[row_num], stream.str());
    }

    // Test AppendRows() in a mode where it appends all of the rows at once.
    // We set max_num_bytes to 1MB to ensure that this happens. We use the
    // same |stream| that we used above so that it already contains the header
    // row.
    row_iterator.Reset();
    max_num_bytes = 1024 * 1024;
    status = serializer.AppendRows(max_num_bytes, &row_iterator, &stream);
    EXPECT_TRUE(status.ok()) << status.error_message() << " ";
    EXPECT_EQ(expected_serialization, stream.str());
  }

 protected:
  grpc::Status SerializeReport(const ReportConfig& report_config,
                               const ReportMetadataLite& metadata,
                               const std::vector<ReportRow>& report_rows,
                               std::string* serialized_report_out,
                               std::string* mime_type_out) {
    ReportSerializer serializer(&report_config, &metadata,
                                &report_config.export_configs(0));
    CHECK_EQ(report_config.export_configs_size(), 1);
    return serializer.SerializeReport(report_rows, serialized_report_out,
                                      mime_type_out);
  }

  std::shared_ptr<ReportRegistry> report_registry_;
};

// Tests the function SerializeReport in the case that the
// report is a histogram report with zero rows added.
TEST_F(ReportSerializerTest, SerializeHistogramReportToCSVNoRows) {
  std::vector<ReportRow> report_rows;
  const char* kExpectedCSV = R"(date,Fruit,count,err
)";
  DoSerializeHistogramReportTest(1, 0, report_rows, "text/csv", kExpectedCSV);
}

// Tests the function SerializeReport in the case that the
// report is a histogram report with rows added whose values are integers,
// and the export is to csv.
TEST_F(ReportSerializerTest, SerializeHistogramReportToCSVIntegerRows) {
  std::vector<ReportRow> report_rows;
  report_rows.push_back(HistogramReportIntValueRow(123, 456.7, 8.0));
  report_rows.push_back(HistogramReportIntValueRow(0, 77777, 0.000001));
  report_rows.push_back(HistogramReportIntValueRow(-1001, 0.019999999, 0.01));
  const char* kExpectedCSV = R"(date,City,count,err
2035-10-22,123,456.700,8.000
2035-10-22,0,77777.000,0
2035-10-22,-1001,0.020,0.010
)";
  DoSerializeHistogramReportTest(1, 1, report_rows, "text/csv", kExpectedCSV);
}

// Tests the function SerializeReport in the case that the
// report is a histogram report with rows added whose values are strings,
// and the export is to csv.
TEST_F(ReportSerializerTest, SerializeHistogramReportToCSVStringRows) {
  std::vector<ReportRow> report_rows;
  report_rows.push_back(HistogramReportStringValueRow("", 0.000001, 1.000001));
  report_rows.push_back(HistogramReportStringValueRow("apple", -7, -77777));
  report_rows.push_back(
      HistogramReportStringValueRow("banana", -7.77777, -77.0000007));
  report_rows.push_back(
      HistogramReportStringValueRow("My \"favorite\" fruit!", 3, 0));
  report_rows.push_back(HistogramReportStringValueRow("\n \r \t \v", 4, 0));
  report_rows.push_back(HistogramReportStringValueRow(
      "This string has length greater than 256 "
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
      0.019999999, 0.01));
  const char* kExpectedCSV = R"(date,Fruit,count,err
2035-10-22,"",0,1.000
2035-10-22,"apple",0,0
2035-10-22,"banana",0,0
2035-10-22,"My %22favorite%22 fruit!",3.000,0
2035-10-22,"%0A %0D %09 %0B",4.000,0
2035-10-22,"This string has length greater than 256 xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",0.020,0.010
)";
  DoSerializeHistogramReportTest(1, 0, report_rows, "text/csv", kExpectedCSV);
}

// Tests the function SerializeReport in the case that the
// report is a histogram report with rows added whose values are blobs,
// and the export is to csv.
TEST_F(ReportSerializerTest, SerializeHistogramReportToCSVBlobRows) {
  std::vector<ReportRow> report_rows;
  report_rows.push_back(HistogramReportBlobValueRow("blob a", 100, 0.1));
  report_rows.push_back(HistogramReportBlobValueRow("blob b", 50, 0));
  const char* kExpectedCSV = R"(date,City,count,err
2035-10-22,bNJoxyQ/fmpYIi0JdGT62jdYZvZr1Qfh/3Ka+XHRPkc=,100.000,0.100
2035-10-22,2aOnR4wmTEA2+lCg37Ocv9A6UdTx5rUJ4okYcaVBZ5s=,50.000,0
)";
  DoSerializeHistogramReportTest(1, 1, report_rows, "text/csv", kExpectedCSV);
}

// Tests the function SerializeReport in the case that the
// report is a histogram report with rows added whose values are indices,
// and the export is to csv.
// Note that when a row with an index has no label and a zero value it
// should be skipped.
TEST_F(ReportSerializerTest, SerializeHistogramReportToCSVIndexRows) {
  std::vector<ReportRow> report_rows;
  report_rows.push_back(HistogramReportIndexValueRow(0, "apple", 100, 0.1));
  report_rows.push_back(HistogramReportIndexValueRow(1, "banana", 50, 0));
  report_rows.push_back(HistogramReportIndexValueRow(2, "", 51, 0));
  report_rows.push_back(HistogramReportIndexValueRow(3, "", 0, 0));
  report_rows.push_back(HistogramReportIndexValueRow(4, "plum", 52, 0));
  const char* kExpectedCSV = R"(date,Fruit,count,err
2035-10-22,"apple",100.000,0.100
2035-10-22,"banana",50.000,0
2035-10-22,<index 2>,51.000,0
2035-10-22,"plum",52.000,0
)";
  DoSerializeHistogramReportTest(1, 0, report_rows, "text/csv", kExpectedCSV);
}

// Tests the function SerializeReport in the case that the
// report is a histogram report with one histogram row with an invalid value,
// and the export is to csv.
TEST_F(ReportSerializerTest, SerializeHistogramReportToCSVInvalidValue) {
  std::vector<ReportRow> report_rows;
  ReportRow report_row;
  report_row.mutable_histogram();
  report_rows.push_back(report_row);
  const char* kExpectedCSV = R"(date,City,count,err
2035-10-22,<Unrecognized value data type>,0,0
)";
  DoSerializeHistogramReportTest(1, 1, report_rows, "text/csv", kExpectedCSV);
}

// Tests that if we use ReportExportConfig 2, which is invalid, that
// INVALID_ARGUMENT is returned (and we don't crash.)
TEST_F(ReportSerializerTest, InvalidReportExportConfig) {
  std::string mime_type;
  std::string report;
  std::vector<ReportRow> report_rows;
  auto status =
      SerializeHistogramReport(2, 0, report_rows, &report, &mime_type);
  EXPECT_EQ(grpc::INVALID_ARGUMENT, status.error_code());
}

// Tests that if the ReportMetadataLite has no variable indices then
// INVALID_ARGUMENT is returned (and we don't crash.)
TEST_F(ReportSerializerTest, InvalidMetadataNoVariableIndices) {
  std::string mime_type;
  std::string report;
  std::vector<ReportRow> report_rows;
  const auto* report_config = report_registry_->Get(kCustomerId, kProjectId, 1);
  CHECK(report_config);
  ReportMetadataLite metadata;
  auto status = SerializeReport(*report_config, metadata, report_rows, &report,
                                &mime_type);
  EXPECT_EQ(grpc::INVALID_ARGUMENT, status.error_code());
}

// Tests that if the ReportMetadataLite has two variable indices then
// INVALID_ARGUMENT is returned (and we don't crash.)
TEST_F(ReportSerializerTest, InvalidMetadataTwoVariableIndices) {
  std::string mime_type;
  std::string report;
  std::vector<ReportRow> report_rows;
  const auto* report_config = report_registry_->Get(kCustomerId, kProjectId, 1);
  CHECK(report_config);
  ReportMetadataLite metadata;
  metadata.add_variable_indices(0);
  metadata.add_variable_indices(1);
  auto status = SerializeReport(*report_config, metadata, report_rows, &report,
                                &mime_type);
  EXPECT_EQ(grpc::INVALID_ARGUMENT, status.error_code());
}

// Tests that if the ReportMetadataLite has an out-of-bounds variable index then
// INVALID_ARGUMENT is returned (and we don't crash.)
TEST_F(ReportSerializerTest, InvalidMetadataIndexOutOfBounds) {
  std::string mime_type;
  std::string report;
  std::vector<ReportRow> report_rows;
  const auto* report_config = report_registry_->Get(kCustomerId, kProjectId, 1);
  CHECK(report_config);
  ReportMetadataLite metadata;
  metadata.add_variable_indices(2);
  auto status = SerializeReport(*report_config, metadata, report_rows, &report,
                                &mime_type);
  EXPECT_EQ(grpc::INVALID_ARGUMENT, status.error_code());
}

// Tests that if the ReportMetadataLite has an unimplemented report type then
// UNIMPLEMENTED is returned (and we don't crash.)
TEST_F(ReportSerializerTest, InvalidMetadataUnimplementedReportType) {
  std::string mime_type;
  std::string report;
  std::vector<ReportRow> report_rows;
  const auto* report_config = report_registry_->Get(kCustomerId, kProjectId, 1);
  CHECK(report_config);
  ReportMetadataLite metadata;
  metadata.add_variable_indices(0);
  metadata.set_report_type(ReportType::JOINT);
  auto status = SerializeReport(*report_config, metadata, report_rows, &report,
                                &mime_type);
  EXPECT_EQ(grpc::UNIMPLEMENTED, status.error_code());
}

// Tests the function SerializeReport in the case that the
// report is a histogram report with one row of the wrong row type.
TEST_F(ReportSerializerTest, InvalidRowNonMatchingRowType) {
  std::string mime_type;
  std::string report;
  std::vector<ReportRow> report_rows;
  ReportRow report_row;
  report_row.mutable_joint();
  report_rows.push_back(report_row);
  const auto* report_config = report_registry_->Get(kCustomerId, kProjectId, 1);
  CHECK(report_config);
  auto status = SerializeReport(*report_config, BuildHistogramMetadata(0),
                                report_rows, &report, &mime_type);
  EXPECT_EQ(grpc::INTERNAL, status.error_code());
}

// Tests the function SerializeReport in the case that the
// report is a histogram report with one row with no row type set.
TEST_F(ReportSerializerTest, InvalidRowNoRowType) {
  std::string mime_type;
  std::string report;
  std::vector<ReportRow> report_rows;
  report_rows.push_back(ReportRow());
  const auto* report_config = report_registry_->Get(kCustomerId, kProjectId, 1);
  CHECK(report_config);
  auto status = SerializeReport(*report_config, BuildHistogramMetadata(0),
                                report_rows, &report, &mime_type);
  EXPECT_EQ(grpc::INTERNAL, status.error_code());
}

}  // namespace analyzer
}  // namespace cobalt
