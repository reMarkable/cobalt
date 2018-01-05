// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_EXPORTER_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_EXPORTER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/report_master/report_internal.pb.h"
#include "config/report_configs.pb.h"
#include "grpc++/grpc++.h"
#include "util/gcs/gcs_util.h"

namespace cobalt {
namespace analyzer {

// An abstract interface for uploading serialized reports to Google Cloud
// Storage. This allows us to mock out the upload step in unit tests.
class GcsUploadInterface {
 public:
  virtual ~GcsUploadInterface() = default;
  virtual grpc::Status UploadToGCS(const std::string& bucket,
                                   const std::string& path,
                                   const std::string& mime_type,
                                   std::istream* report_stream) = 0;
};

// An implementation of GcsUploadInterface that actually uploads files to
// Google Cloud Storage.
class GcsUploader : public GcsUploadInterface {
 public:
  virtual ~GcsUploader() = default;

  grpc::Status UploadToGCS(const std::string& bucket, const std::string& path,
                           const std::string& mime_type,
                           std::istream* report_stream) override;

 private:
  grpc::Status PingBucket(const std::string& bucket);

  std::unique_ptr<util::gcs::GcsUtil> gcs_util_;
};

// A ReportExporter is used by the ReportMaster to export serialized reports
// to external systems.
//
// Usage:
// Construct an instance of ReportExporter passing in an instance of
// GcsUploadInterface. In production this should be an instance of
// GcsUploader but in a test this may be a mock. This instance may
// live for the lifetime of the program, but it is not thread-safe.
// Invoke ExportReport() repeatedly.
class ReportExporter {
 public:
  // Constructs an instance of ReportExporter that will use |uploader| for
  // uploading to Google Cloud Storage.
  explicit ReportExporter(std::shared_ptr<GcsUploadInterface> uploader);

  // Serializes and exports the provided report to an external system, or else
  // immediately returns grpc::OK, based on the data in the provided parameters,
  // as described below.
  //
  // report_config: The ExportConfigs from here determine how to serialize the
  // report (for example to a CSV file) and the locations of where to export
  // the report (for example to a Google Cloud Storage bucket.)  In the case of
  // exporting to Google Cloud Storage, the ExportConfig specifies a bucket and
  // a folder path, but not the file name. That is taken from the |export_name|
  // field of |metadata|. If the |export_name| field of |metadata| is not set
  // then no exporting will be done and this method will immediately return
  // grpc::OK.
  //
  // There may be any number of ExportConfigs. If there are zero ExportConfigs
  // then no exporting will be done and this method will immediately
  // return grpc::OK. If there are one or more ExportConfigs then the
  // report will be exported to one or more locations and grpc::OK will be
  // returned just in case all of the exports are successful.
  //
  // Also, the metric part names within the ReportVariables are taken from
  // |report_config| and used as the column headers for the value columns of
  // the serialized report. Note that the |report_type| is not taken from here
  // but rather from |metadata|. This is because the report being serialized may
  // be an auxilliary report rather than the primary report for the
  // ReportConfig.
  //
  // metadata. As described previously, if the |export_name| field
  // from here is empty then no exporting will be done and this method will
  // immediately return grpc::OK. Otherwise the |export_name| field specifes
  // part of the location of where to export the file.
  //
  // Also the |report_type| is taken from |metadata|. Also the list of
  // |variable_indices| from here determines which ReportVariables from
  // |report_config| are used, and their order.
  //
  // report_rows: The actual row data to be serialized and exported. The type
  // of the rows must correspond to the |report_type| from |metadata|.
  //
  // Returns grpc::OK if either no exporter was done, or if all exporting
  // was successful. Otherwise logs an error and returns some other status.
  grpc::Status ExportReport(const ReportConfig& report_config,
                            const ReportMetadataLite& metadata,
                            const std::vector<ReportRow>& report_rows);

 private:
  friend class ReportExporterTest;

  std::shared_ptr<GcsUploadInterface> uploader_;

  grpc::Status ExportReportOnce(const ReportConfig& report_config,
                                const ReportMetadataLite& metadata,
                                const ReportExportConfig& export_config,
                                const std::vector<ReportRow>& report_rows);

  grpc::Status ExportReportToGCS(const ReportConfig& report_config,
                                 const GCSExportLocation& location,
                                 const ReportMetadataLite& metadata,
                                 const std::string& mime_type,
                                 std::istream* report_stream);

  // Builds a file path for exporting to Google Cloud Storage by concatenating
  // a folder path based on the report config id, the metadata export_name,
  // and a dot extension based on the mime type.
  static std::string GcsPath(const ReportConfig& report_config,
                             const ReportMetadataLite& metadata,
                             const std::string& mime_type);
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_EXPORTER_H_
