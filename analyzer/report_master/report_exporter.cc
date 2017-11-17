// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_exporter.h"

#include <memory>
#include <sstream>
#include <thread>

#include "analyzer/report_master/report_serializer.h"
#include "glog/logging.h"

namespace cobalt {
namespace analyzer {

using util::gcs::GcsUtil;

ReportExporter::ReportExporter(std::shared_ptr<GcsUploadInterface> uploader)
    : uploader_(uploader) {}

grpc::Status ReportExporter::ExportReport(
    const ReportConfig& report_config, const ReportMetadataLite& metadata,
    const std::vector<ReportRow>& report_rows) {
  if (metadata.export_name().empty()) {
    // If we were not told to export this report, there is nothing to do.
    return grpc::Status::OK;
  }

  grpc::Status overall_status = grpc::Status::OK;
  for (const auto& export_config : report_config.export_configs()) {
    auto status =
        ExportReportOnce(report_config, metadata, export_config, report_rows);
    if (!status.ok()) {
      overall_status = status;
    }
  }
  return overall_status;
}

grpc::Status ReportExporter::ExportReportOnce(
    const ReportConfig& report_config, const ReportMetadataLite& metadata,
    const ReportExportConfig& export_config,
    const std::vector<ReportRow>& report_rows) {
  std::string serialized_report;
  std::string mime_type;
  ReportSerializer serializer;
  auto status =
      serializer.SerializeReport(report_config, metadata, export_config,
                                 report_rows, &serialized_report, &mime_type);
  if (!status.ok()) {
    return status;
  }

  auto location_case = export_config.export_location_case();
  switch (location_case) {
    case ReportExportConfig::kGcs:
      return ExportReportToGCS(export_config.gcs(), metadata, mime_type,
                               serialized_report);
      break;

    default: {
      std::ostringstream stream;
      stream << "Unrecognized export_location: " << location_case;
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::INTERNAL, message);
    }
  }
}

grpc::Status ReportExporter::ExportReportToGCS(
    const GCSExportLocation& location, const ReportMetadataLite& metadata,
    const std::string& mime_type, const std::string& serialized_report) {
  if (location.bucket().empty()) {
    std::string message = "CSVExportLocation has empty |bucket|";
    LOG(ERROR) << message;
    return grpc::Status(grpc::INVALID_ARGUMENT, message);
  }

  return uploader_->UploadToGCS(
      location.bucket(),
      FormFullPath(location.folder_path(), metadata.export_name()), mime_type,
      serialized_report);
}

std::string ReportExporter::FormFullPath(const std::string& folder_path,
                                         const std::string& file_name) {
  std::ostringstream stream;
  stream << folder_path;
  if (folder_path.back() != '/') {
    stream << "/";
  }
  stream << file_name;
  return stream.str();
}

grpc::Status GcsUploader::UploadToGCS(const std::string& bucket,
                                      const std::string& path,
                                      const std::string& mime_type,
                                      const std::string& serialized_report) {
  if (!gcs_util_) {
    gcs_util_.reset(new GcsUtil());
    if (!gcs_util_->InitFromDefaultPaths()) {
      gcs_util_.reset();
      std::string message = "Unable to initialize GcsUtil.";
      LOG(ERROR) << message;
      return grpc::Status(grpc::INTERNAL, message);
    }
    auto status = PingBucket(bucket);
    if (!status.ok()) {
      gcs_util_.reset();
      return status;
    }
  }
  int seconds_to_sleep = 1;
  for (int i = 0; i < 5; i++) {
    if (gcs_util_->Upload(bucket, path, mime_type, serialized_report.data(),
                          serialized_report.size())) {
      return grpc::Status::OK;
    }
    if (i < 4) {
      LOG(WARNING) << "Upload to GCS at " << bucket << "|" << path
                   << " failed. Sleeping for " << seconds_to_sleep
                   << " seconds before trying again.";
      std::this_thread::sleep_for(std::chrono::seconds(seconds_to_sleep));
      seconds_to_sleep *= 2;
    }
  }
  gcs_util_.reset();
  std::ostringstream stream;
  stream << "Upload to GCS at " << bucket << "|" << path
         << " failed five times. Giving up.";
  std::string message = stream.str();
  LOG(ERROR) << message;
  return grpc::Status(grpc::INTERNAL, message);
}

grpc::Status GcsUploader::PingBucket(const std::string& bucket) {
  if (!gcs_util_) {
    gcs_util_.reset(new GcsUtil());
    if (!gcs_util_->InitFromDefaultPaths()) {
      gcs_util_.reset();
      std::string message = "Unable to initialize GcsUtil.";
      LOG(ERROR) << message;
      return grpc::Status(grpc::INTERNAL, message);
    }
  }
  int seconds_to_sleep = 1;
  for (int i = 0; i < 5; i++) {
    if (gcs_util_->Ping(bucket)) {
      return grpc::Status::OK;
    }

    if (i < 4) {
      LOG(WARNING) << "Pinging " << bucket << " failed. Sleeping for "
                   << seconds_to_sleep << " seconds before trying again.";
      std::this_thread::sleep_for(std::chrono::seconds(seconds_to_sleep));
      seconds_to_sleep *= 2;
    }
  }
  gcs_util_.reset();
  std::ostringstream stream;
  stream << "Pinging " << bucket << " failed five times. Giving up.";
  std::string message = stream.str();
  LOG(ERROR) << message;
  return grpc::Status(grpc::INTERNAL, message);
}

}  // namespace analyzer
}  // namespace cobalt
