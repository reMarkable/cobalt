// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_SERIALIZER_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_SERIALIZER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/report_master/report_internal.pb.h"
#include "config/report_configs.pb.h"
#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {

// A utility for serializing reports to strings so that they may be exported.
// This is used by ReportExporter.
//
// Usage:
// Construct an instance and then invoke SerializeReport(). Currently
// an instance has no state.
class ReportSerializer {
 public:
  // Serializes a report according to the given parameters, as described below.
  //
  // report_config: The metric part names within the ReportVariables of this
  // ReportConfig are used as the column headers for the value columns of the
  // serialized report. Note that the |report_type| is not taken from here but
  // rather from |metadata|. This is because the report being serialized may be
  // an auxilliary report rather than the primary report for the ReportConfig.
  //
  // metadata: The |report_type| is taken from here. Also the list of
  // |variable_indices| determines which ReportVariables from |report_config|
  // are used, and their order.
  //
  // export_config: The serialization type (e.g. CSV) is taken from here.
  //
  // report_rows: The actual row data to be serialized. The type of the rows
  // must correspond to the |report_type| from |metadata|.
  //
  // serialed_report_out: On success, the string pointed to will contain the
  // serialized report.
  //
  // mime_type_out: On success, the string pointed to will contain the MIME
  // type of the generated report.
  //
  // Returns OK on success. Logs an ERROR and returns a different status
  // if the data contained in the arguments is not self-consistent.
  grpc::Status SerializeReport(const ReportConfig& report_config,
                               const ReportMetadataLite& metadata,
                               const ReportExportConfig& export_config,
                               const std::vector<ReportRow>& report_rows,
                               std::string* serialized_report_out,
                               std::string* mime_type_out);

 private:
  grpc::Status SerializeReportToCSV(const ReportConfig& report_config,
                                    const ReportMetadataLite& metadata,
                                    const std::vector<ReportRow>& report_rows,
                                    std::string* serialized_report_out,
                                    std::string* mime_type_out);

  grpc::Status AppendCSVHeaderRow(
      const ReportConfig& report_config, const ReportMetadataLite& metadata,
      size_t* num_columns_out,
      std::vector<std::string>* fixed_leftmost_column_values_out,
      std::ostringstream* stream);

  grpc::Status AppendCSVHistogramHeaderRow(
      const ReportConfig& report_config, const ReportMetadataLite& metadata,
      size_t* num_columns_out,
      std::vector<std::string>* fixed_leftmost_column_values_out,
      std::ostringstream* stream);

  grpc::Status AppendCSVJointHeaderRow(
      const ReportConfig& report_config, const ReportMetadataLite& metadata,
      size_t* num_columns_out,
      std::vector<std::string>* fixed_leftmost_column_values_out,
      std::ostringstream* stream);

  grpc::Status AppendCSVHeaderRowVariableNames(
      const ReportConfig& report_config, const ReportMetadataLite& metadata,
      std::ostringstream* stream);

  grpc::Status AppendCSVReportRow(
      const ReportConfig& report_config, const ReportMetadataLite& metadata,
      const ReportRow& report_row, size_t num_columns,
      const std::vector<std::string>& fixed_leftmost_column_values,
      std::ostringstream* stream);

  grpc::Status AppendCSVHistogramReportRow(
      const HistogramReportRow& report_row, size_t num_columns,
      const std::vector<std::string>& fixed_leftmost_column_values,
      std::ostringstream* stream);

  grpc::Status AppendCSVJointReportRow(
      const JointReportRow& report_row, size_t num_columns,
      const std::vector<std::string>& fixed_leftmost_column_values,
      std::ostringstream* stream);
};

}  // namespace analyzer
}  // namespace cobalt
#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_SERIALIZER_H_
