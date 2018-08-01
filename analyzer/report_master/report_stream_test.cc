// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_stream.h"

#include <memory>
#include <sstream>
#include <vector>

#include "config/config_text_parser.h"
#include "config/report_config.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {

using config::ReportRegistry;

namespace {

const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;
const uint32_t kReportConfigId = 1;
const uint32_t kSomeDayIndex = 123456;

const char* kReportConfigText = R"(
element {
  customer_id: 1
  project_id: 1
  id: 1
  metric_id: 1
  variable {
    metric_part: "Rating"
  }
  export_configs {
    csv {}
  }
}

)";

ReportMetadataLite BuildHistogramMetadata() {
  ReportMetadataLite metadata;
  metadata.set_report_type(ReportType::HISTOGRAM);
  metadata.add_variable_indices(0);
  metadata.set_first_day_index(kSomeDayIndex);
  metadata.set_last_day_index(kSomeDayIndex);
  return metadata;
}

ReportRow HistogramReportIntValueRow(int value) {
  ReportRow report_row;
  HistogramReportRow* row = report_row.mutable_histogram();
  row->mutable_value()->set_int_value(value);
  return report_row;
}

// A FakeReportRowIterator is a ReportRowIterator that will do the following:
// (1) Return OK |num_success_first| times with the returned row being
// |first_report_row|.
// (2) Return |middle_status| one time with the returned row being
// |first_report_row|.
// (1) Return OK |num_success_second| times with the returned row being
// |second_report_row|.
struct FakeReportRowIterator : public ReportRowIterator {
  size_t num_success_first;
  ReportRow first_report_row;
  grpc::Status middle_status;
  size_t num_success_second;
  ReportRow second_report_row;

  grpc::Status Reset() override {
    index_ = 0;
    return grpc::Status::OK;
  }

  grpc::Status NextRow(const ReportRow** row) override {
    index_++;
    if (index_ <= num_success_first) {
      *row = &first_report_row;
      return grpc::Status::OK;
    } else if (index_ == num_success_first + 1) {
      *row = &first_report_row;
      return middle_status;
    } else if (index_ <= num_success_first + num_success_second + 1) {
      *row = &second_report_row;
      return grpc::Status::OK;
    } else {
      return grpc::Status(grpc::NOT_FOUND, "EOF");
    }
  }

  grpc::Status HasMoreRows(bool* b) override {
    CHECK(b);
    *b = (index_ < num_success_first + num_success_second + 1);
    return grpc::Status::OK;
  }

 private:
  size_t index_ = 0;
};

}  // namespace

// Tests of ReportStream.
class ReportStreamTest : public ::testing::Test {
 public:
  void SetUp() {
    // Parse the report config string
    auto report_parse_result =
        config::FromString<RegisteredReports>(kReportConfigText, nullptr);
    EXPECT_EQ(config::kOK, report_parse_result.second);
    report_registry_.reset((report_parse_result.first.release()));
  }

  // Performs the following test actions:
  // - Constructs a ReportSerializer for our one static ReportConfig.
  // -Constructs a ReportStream wrapping that ReportSerializer and the given
  //  |row_iterator|, using the given |buffer_size|.
  // - Invokes Start() on the ReportStream.
  //   -- Checks that the status is |expected_start_status|.
  //   -- Checks that the mime type is |expected_mime_type|.
  // - Reads the entire serialized report from the ReportStream into a string.
  //   -- Checks that the status is |expected_end_status|.
  //   -- Checks that the serialized report is |expected_serialization|.
  void DoStreamTestWithBufferSize(ReportRowIterator* row_iterator,
                                  const std::string expected_mime_type,
                                  const std::string& expected_serialization,
                                  grpc::StatusCode expected_start_status,
                                  grpc::StatusCode expected_end_status,
                                  size_t buffer_size) {
    // Construct a ReportSerializer.
    auto metadata = BuildHistogramMetadata();
    auto report_config =
        report_registry_->Get(kCustomerId, kProjectId, kReportConfigId);
    ReportSerializer serializer(report_config, &metadata,
                                &(report_config->export_configs(0)));

    // Construct a ReportStream wrapping |serializer| and |row_iterator| that
    //  uses the given |buffer_size|.
    ReportStream report_stream(&serializer, row_iterator, buffer_size);
    // Invoke Start() and check the MIME type and status.
    auto status = report_stream.Start();
    EXPECT_EQ(expected_start_status, status.error_code())
        << status.error_message() << " ";
    EXPECT_EQ(expected_mime_type, report_stream.mime_type());

    for (auto test_iteration = 0; test_iteration < 3; test_iteration++) {
      if (status.ok()) {
        // Test that before reading from the stream, tellg() returns zero.
        EXPECT_EQ(0, report_stream.tellg())
            << "test_iteration=" << test_iteration
            << " tellg() = " << report_stream.tellg();

        // This should be a no-op seekg.
        report_stream.seekg(0);
        // tellg() should again return 0
        EXPECT_EQ(0, report_stream.tellg())
            << "test_iteration=" << test_iteration
            << " tellg() = " << report_stream.tellg();
      }

      // Read the entire serialized report from the ReportStream into a string.
      std::string serialized_report(
          std::istreambuf_iterator<char>(report_stream), {});

      // Check the serialized report.
      EXPECT_EQ(expected_serialization.size(), serialized_report.size())
          << "test_iteration=" << test_iteration;
      EXPECT_EQ(expected_serialization, serialized_report)
          << "test_iteration=" << test_iteration;

      // Check the status.
      status = report_stream.status();
      EXPECT_EQ(expected_end_status, status.error_code())
          << status.error_message() << " "
          << " test_iteration=" << test_iteration;

      if (!report_stream.status().ok()) {
        EXPECT_TRUE(report_stream.fail());
        EXPECT_TRUE(report_stream.bad());
        EXPECT_FALSE(report_stream.good());
      } else {
        EXPECT_FALSE(report_stream.fail());
        EXPECT_FALSE(report_stream.bad());
        EXPECT_TRUE(report_stream.good());
        EXPECT_NE(0, report_stream.tellg());
      }
      report_stream.clear();
      report_stream.seekg(0);
    }
  }

  // Invokes DoStreamTestWithBufferSize() four times with buffer sizes
  // 1, 10, 20 and 1MB. The smaller values of buffer_size will cause
  // underflow() to be invoked whereas the larger values of buffer_size
  // will cause the entire report to be read during Start().
  void DoStreamTest(ReportRowIterator* row_iterator,
                    const std::string expected_mime_type,
                    const std::string& expected_serialization,
                    grpc::StatusCode expected_start_status,
                    grpc::StatusCode expected_end_status) {
    DoStreamTestWithBufferSize(row_iterator, expected_mime_type,
                               expected_serialization, expected_start_status,
                               expected_end_status, 1);
    row_iterator->Reset();
    DoStreamTestWithBufferSize(row_iterator, expected_mime_type,
                               expected_serialization, expected_start_status,
                               expected_end_status, 10);
    row_iterator->Reset();
    DoStreamTestWithBufferSize(row_iterator, expected_mime_type,
                               expected_serialization, expected_start_status,
                               expected_end_status, 20);
    row_iterator->Reset();
    DoStreamTestWithBufferSize(row_iterator, expected_mime_type,
                               expected_serialization, expected_start_status,
                               expected_end_status, 1024 * 1024);
  }

 private:
  std::shared_ptr<ReportRegistry> report_registry_;
};

// Tests a ReportStream when the RowIterator yields no rows.
TEST_F(ReportStreamTest, NoRows) {
  std::vector<ReportRow> report_rows;
  ReportRowVectorIterator row_iterator(&report_rows);
  const char* kExpectedCSV = R"(date,Rating,count,err
)";
  DoStreamTest(&row_iterator, "text/csv", kExpectedCSV, grpc::OK, grpc::OK);
}

// Tests a ReportStream when the RowIterator yields a small number of rows.
TEST_F(ReportStreamTest, SomeRows) {
  std::vector<ReportRow> report_rows;
  for (int i = 0; i < 20; i++) {
    report_rows.push_back(HistogramReportIntValueRow(i));
  }
  ReportRowVectorIterator row_iterator(&report_rows);
  const char* kExpectedCSV = R"(date,Rating,count,err
2035-10-22,0,0,0
2035-10-22,1,0,0
2035-10-22,2,0,0
2035-10-22,3,0,0
2035-10-22,4,0,0
2035-10-22,5,0,0
2035-10-22,6,0,0
2035-10-22,7,0,0
2035-10-22,8,0,0
2035-10-22,9,0,0
2035-10-22,10,0,0
2035-10-22,11,0,0
2035-10-22,12,0,0
2035-10-22,13,0,0
2035-10-22,14,0,0
2035-10-22,15,0,0
2035-10-22,16,0,0
2035-10-22,17,0,0
2035-10-22,18,0,0
2035-10-22,19,0,0
)";
  DoStreamTest(&row_iterator, "text/csv", kExpectedCSV, grpc::OK, grpc::OK);
}

// Tests a ReportStream when the RowIterator yields a large number of rows.
// In particulr we make sure overflow() will be invoked several times.
TEST_F(ReportStreamTest, ManyRows) {
  FakeReportRowIterator fake_iterator;
  fake_iterator.num_success_first = 1000;
  fake_iterator.first_report_row = HistogramReportIntValueRow(1);
  fake_iterator.middle_status = grpc::Status::OK;
  fake_iterator.num_success_second = 1000;
  fake_iterator.second_report_row = HistogramReportIntValueRow(2);
  std::ostringstream expected_stream;
  expected_stream << "date,Rating,count,err\n";
  for (int i = 0; i <= 1000; i++) {
    expected_stream << "2035-10-22,1,0,0\n";
  }
  for (int i = 0; i < 1000; i++) {
    expected_stream << "2035-10-22,2,0,0\n";
  }
  DoStreamTest(&fake_iterator, "text/csv", expected_stream.str(), grpc::OK,
               grpc::OK);
}

// Tests a ReportStream when the RowIterator yields some rows, then returns
// an error, then is willing to yield more rows. We expect the second batch
// of rows won't be requested.
TEST_F(ReportStreamTest, MiddleError) {
  FakeReportRowIterator fake_iterator;
  fake_iterator.num_success_first = 1000;
  fake_iterator.first_report_row = HistogramReportIntValueRow(1);
  fake_iterator.middle_status =
      grpc::Status(grpc::DEADLINE_EXCEEDED, "Timeout");
  fake_iterator.num_success_second = 1000;
  fake_iterator.second_report_row = HistogramReportIntValueRow(2);
  // The expected stream should contain just the first batch of rows
  // before the DEADLINE_EXCEEDED occurred. The latter batch of rows should
  // never be requested.
  std::ostringstream expected_stream;
  expected_stream << "date,Rating,count,err\n";
  for (int i = 0; i < 1000; i++) {
    expected_stream << "2035-10-22,1,0,0\n";
  }
  std::string expected_serialization = expected_stream.str();

  // Test once with a buffer size that will cause Start() to succeed and then
  // the DEADLINE_EXCEEDED to occur during underflow().
  DoStreamTestWithBufferSize(&fake_iterator, "text/csv", expected_serialization,
                             grpc::OK, grpc::DEADLINE_EXCEEDED, 1024);

  // Test once with a larger buffer size that will cause the entire report to
  // be read during Start() and so Start() will see the DEADLINE_EXCEEDED.
  fake_iterator.Reset();
  DoStreamTestWithBufferSize(&fake_iterator, "text/csv", expected_serialization,
                             grpc::DEADLINE_EXCEEDED, grpc::DEADLINE_EXCEEDED,
                             1024 * 1024);
}

// Tests a ReportSttream when the RowIterator yields some rows, then starts
// yielding bad rows that cannot be serialized.
TEST_F(ReportStreamTest, BadSecondRowType) {
  FakeReportRowIterator fake_iterator;
  fake_iterator.num_success_first = 1000;
  fake_iterator.first_report_row = HistogramReportIntValueRow(1);
  fake_iterator.middle_status = grpc::Status::OK;
  fake_iterator.num_success_second = 1000;
  fake_iterator.second_report_row = ReportRow();  // This is an invalid row.

  // The expected stream should contain the first batch of rows and the
  // middle row but not the second batch of rows.
  std::ostringstream expected_stream;
  expected_stream << "date,Rating,count,err\n";
  for (int i = 0; i <= 1000; i++) {
    expected_stream << "2035-10-22,1,0,0\n";
  }
  std::string expected_serialization = expected_stream.str();

  // Test once with a buffer size that will cause Start() to succeed and then
  // the bad rows to be seen during underflow().
  DoStreamTestWithBufferSize(&fake_iterator, "text/csv", expected_serialization,
                             grpc::OK, grpc::INTERNAL, 1024);

  // Test once with a larger buffer size that will cause the entire report to
  // be read during Start() and so Start() will see the bad rows.
  fake_iterator.Reset();
  DoStreamTestWithBufferSize(&fake_iterator, "text/csv", expected_serialization,
                             grpc::INTERNAL, grpc::INTERNAL, 1024 * 1024);
}

// Tests a ReportSttream when the RowIterator yields bad rows that cannot be
// serialized right away.
TEST_F(ReportStreamTest, BadFirstRowType) {
  FakeReportRowIterator fake_iterator;
  fake_iterator.num_success_first = 1000;
  fake_iterator.first_report_row = ReportRow();  // This is an invalid row.
  fake_iterator.middle_status = grpc::Status::OK;
  fake_iterator.num_success_second = 1000;
  fake_iterator.second_report_row = HistogramReportIntValueRow(2);

  // The expected stream should contain only the report header.
  std::ostringstream expected_stream;
  expected_stream << "date,Rating,count,err\n";
  std::string expected_serialization = expected_stream.str();

  // Test once with a small buffer size. The bad row is still encountered during
  // Start().
  DoStreamTestWithBufferSize(&fake_iterator, "text/csv", expected_serialization,
                             grpc::INTERNAL, grpc::INTERNAL, 1024);

  // Test once with a larger buffer size. Again the bad row is encountered
  // during Start().
  fake_iterator.Reset();
  DoStreamTestWithBufferSize(&fake_iterator, "text/csv", expected_serialization,
                             grpc::INTERNAL, grpc::INTERNAL, 1024 * 1024);
}

}  // namespace analyzer
}  // namespace cobalt
