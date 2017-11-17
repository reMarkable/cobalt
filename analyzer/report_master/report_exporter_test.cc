// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_exporter.h"

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

const char* const kReportConfigText = R"(
# ReportConfig 1 is valid with one ExportConfig.
element {
  customer_id: 1
  project_id: 1
  id: 1
  metric_id: 1
  variable {
    metric_part: "Fruit"
  }
  export_configs {
    csv {}
    gcs {
      bucket: "BUCKET-NAME-1"
      folder_path: "report_exporter_test/fruit_counts"
    }
  }
}

# ReportConfig 2 is valid with no ExportConfigs.
element {
  customer_id: 1
  project_id: 1
  id: 2
}

# ReportConfig 3 is valid with two ExportConfig.
element {
  customer_id: 1
  project_id: 1
  id: 3
  metric_id: 1
  variable {
    metric_part: "Fruit"
  }
  export_configs {
    csv {}
    gcs {
      bucket: "BUCKET-NAME-1"
      folder_path: "report_exporter_test/fruit_counts"
    }
  }
  export_configs {
    csv {}
    gcs {
      bucket: "BUCKET-NAME-2"
      folder_path: "report_exporter_test/fruit_counts"
    }
  }
}

# ReportConfig 4 has an invalid ExportConfig.
element {
  customer_id: 1
  project_id: 1
  id: 4
  metric_id: 1
  # This export_config is invalid.
  export_configs {
  }
}
)";

// Returns the above report config text, but with the given |bucket_name|
// as the name of the bucket in the ExportConfig for ReportConfig 1.
std::string ReplaceBucketName(const std::string& bucket_name) {
  std::string report_config_text(kReportConfigText);
  size_t index = report_config_text.find("BUCKET-NAME-1");
  return report_config_text.replace(index, std::strlen("BUCKET-NAME-1"),
                                    bucket_name);
}

// This is the CSV that should be generated based on the rows that are
// added to the report in ReportExporterTest::ExportReport().
const char* const kExpectedCSV = R"(Fruit,count,err
"apple",10.000,0
"banana",15.000,0.100
"cantaloup",7.100,0
)";

// Builds a ReportMetadataLite of type HISTOGRAM with one variable for
// index - and the given export_name.
ReportMetadataLite BuildHistogramMetadata(const std::string& export_name) {
  ReportMetadataLite metadata;
  metadata.set_report_type(ReportType::HISTOGRAM);
  metadata.add_variable_indices(0);
  metadata.set_export_name(export_name);
  return metadata;
}

ReportRow HistogramReportStringValueRow(const std::string& value,
                                        float count_estimate, float std_error) {
  ReportRow report_row;
  HistogramReportRow* row = report_row.mutable_histogram();
  row->mutable_value()->set_string_value(value);
  row->set_count_estimate(count_estimate);
  row->set_std_error(std_error);
  return report_row;
}

// An implementation of GcsUploadInterface that saves its parameters and
// returns OK.
struct FakeGcsUploader : public GcsUploadInterface {
  grpc::Status UploadToGCS(const std::string& bucket, const std::string& path,
                           const std::string& mime_type,
                           const std::string& serialized_report) override {
    this->upload_was_invoked = true;
    this->bucket = bucket;
    this->path = path;
    this->mime_type = mime_type;
    this->serialized_report = serialized_report;
    return grpc::Status::OK;
  }

  bool upload_was_invoked = false;
  std::string bucket;
  std::string path;
  std::string mime_type;
  std::string serialized_report;
};

}  // namespace

class ReportExporterTest : public ::testing::Test {
 public:
  void SetUp() {
    // Parse the report config string
    auto report_parse_result =
        ReportRegistry::FromString(kReportConfigText, nullptr);
    EXPECT_EQ(config::kOK, report_parse_result.second);
    report_registry_.reset((report_parse_result.first.release()));
    fake_uploader_.reset(new FakeGcsUploader());
    report_exporter_.reset(new ReportExporter(fake_uploader_));
  }

  // Invokes ReportExporter::ExportReport() with the ReportConfig corresponding
  // to the give report_config_id, with metadata containing the given
  // export_name, and with a fixed set of rows.
  grpc::Status ExportReport(uint32_t report_config_id,
                            const std::string& export_name) {
    const auto* report_config =
        report_registry_->Get(kCustomerId, kProjectId, report_config_id);
    CHECK(report_config);
    std::vector<ReportRow> report_rows;
    report_rows.push_back(HistogramReportStringValueRow("apple", 10, 0));
    report_rows.push_back(HistogramReportStringValueRow("banana", 15, 0.1));
    report_rows.push_back(HistogramReportStringValueRow("cantaloup", 7.1, 0));
    auto metadata = BuildHistogramMetadata(export_name);
    return report_exporter_->ExportReport(*report_config, metadata,
                                          report_rows);
  }

 protected:
  std::shared_ptr<ReportRegistry> report_registry_;
  std::shared_ptr<FakeGcsUploader> fake_uploader_;
  std::unique_ptr<ReportExporter> report_exporter_;
};

// Tests that if there is no export_name specified in the report metadata, then
// ExportReport() does nothing and returns OK.
TEST_F(ReportExporterTest, NoExportName) {
  // ReportConfig 1 has one valid ExportConfig.
  // We use an empty export_name.
  auto status = ExportReport(1, "");
  // Expect the invocation of ExportReport() to succeed.
  EXPECT_TRUE(status.ok()) << status.error_message();
  // But expect that no actual exporting occurred.
  EXPECT_FALSE(fake_uploader_->upload_was_invoked);
}

// Tests that if there is an export_name specified in the report metadata, but
// no ExportConfigs in the ReportConfig, then ExportReport() does nothing and
// returns OK.
TEST_F(ReportExporterTest, NoExportConfigs) {
  // ReportConfig 2 has no valid ExportConfigs.
  // We use a non-empty export name
  auto status = ExportReport(2, "export_name");
  // Expect the invocation of ExportReport() to succeed.
  EXPECT_TRUE(status.ok()) << status.error_message();
  // But expect that no actual exporting occurred.
  EXPECT_FALSE(fake_uploader_->upload_was_invoked);
}

// Tests a successful export to one location.
TEST_F(ReportExporterTest, OneExportLocation) {
  // ReportConfig 1 has one valid ExportConfig.
  auto status = ExportReport(1, "export_name");
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_TRUE(fake_uploader_->upload_was_invoked);
  EXPECT_EQ("BUCKET-NAME-1", fake_uploader_->bucket);
  EXPECT_EQ("report_exporter_test/fruit_counts/export_name",
            fake_uploader_->path);
  EXPECT_EQ("text/csv", fake_uploader_->mime_type);
  EXPECT_EQ(kExpectedCSV, fake_uploader_->serialized_report);
}

// Tests a successful export to two location.
TEST_F(ReportExporterTest, TwoExportLocations) {
  // ReportConfig 3 has two valid ExportConfig
  auto status = ExportReport(3, "export_name");
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_TRUE(fake_uploader_->upload_was_invoked);
  // Tests that BUCKET-NAME-2 was used after BUCKET-NAME-1.
  EXPECT_EQ("BUCKET-NAME-2", fake_uploader_->bucket);
  EXPECT_EQ("report_exporter_test/fruit_counts/export_name",
            fake_uploader_->path);
  EXPECT_EQ("text/csv", fake_uploader_->mime_type);
  EXPECT_EQ(kExpectedCSV, fake_uploader_->serialized_report);
}

// Tests that when an invalid ExportConfig is used then ExportReport() returns
// INVALID_ARGUMENT and no exporting occurrs.
TEST_F(ReportExporterTest, InvalidReportConfig) {
  // ReportConfig 4 has an invalid ExportConfig
  auto status = ExportReport(4, "export_name");
  EXPECT_EQ(grpc::INVALID_ARGUMENT, status.error_code());
  EXPECT_FALSE(fake_uploader_->upload_was_invoked);
}

// Tests actually doing a real upload to Google Cloud Storage.
//
// The guts of this test have been commented-out so that they do not executed on
// our CI and CQ bots. A developer may uncomment this and replace the
// three string tokens:
// <put-your-real-bucket-name-here>
// <cobalt-source-root-dir>
// <path-to-your-real-service-acount-key-file-here>
// in order to run the test locally.
TEST_F(ReportExporterTest, RealUploadToGCS) {
  // Parse the report config string
  auto report_parse_result = ReportRegistry::FromString(
      ReplaceBucketName("<put-your-real-bucket-name-here>"), nullptr);
  EXPECT_EQ(config::kOK, report_parse_result.second);
  report_registry_.reset((report_parse_result.first.release()));

  setenv("GRPC_DEFAULT_SSL_ROOTS_FILE_PATH",
         "<cobalt-source-root-dir>/third_party/grpc/etc/roots.pem", 1);
  setenv("GOOGLE_APPLICATION_CREDENTIALS",
         "<path-to-your-real-service-acount-key-file-here>", 1);

  /*

  // Instantiate a ReportExporter using a non-mock GcsUploader.
  ReportExporter report_exporter(
      std::shared_ptr<GcsUploadInterface>(new GcsUploader()));

  const auto* report_config = report_registry_->Get(kCustomerId, kProjectId, 1);
  CHECK(report_config);
  auto metadata = BuildHistogramMetadata("export_name");
  std::vector<ReportRow> report_rows;
  report_rows.push_back(HistogramReportStringValueRow("apple", 10, 0));
  report_rows.push_back(HistogramReportStringValueRow("banana", 15, 0.1));
  report_rows.push_back(HistogramReportStringValueRow("cantaloup", 7.1, 0));
  auto status =
      report_exporter.ExportReport(*report_config, metadata, report_rows);
  EXPECT_TRUE(status.ok()) << status.error_message();
*/
}

}  // namespace analyzer
}  // namespace cobalt
