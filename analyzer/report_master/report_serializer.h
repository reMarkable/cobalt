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
#include "analyzer/report_master/report_row_iterator.h"
#include "config/report_configs.pb.h"
#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {

// A utility for serializing reports to strings or streams so that they may be
// exported. This is used by ReportExporter.
//
// Usage:
// Construct an instance. Then either invoke SerializeReport() to serialize
// the whole report at once to a string, or else invoke StartSerializingReport()
// followed by multiple invocations of AppendRows() in order to serialize the
// report incrementally.
//
// See also ReportStream in report_stream.h.
class ReportSerializer {
 public:
  // Constructor
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
  ReportSerializer(const ReportConfig* report_config,
                   const ReportMetadataLite* metadata,
                   const ReportExportConfig* export_config);

  // Serializes the report described by the parameters passed to the constructor
  // and the parameters passed to this method, as described below.
  //
  // report_rows: The actual row data to be serialized. The type of the rows
  // must correspond to the |report_type| from the |metadata| passed to the
  // constructor.
  //
  // serialized_report_out: On success, the string pointed to will contain the
  // serialized report.
  //
  // mime_type_out: On success, the string pointed to will contain the MIME
  // type of the generated report.
  //
  // Returns OK on success. Logs an ERROR and returns a different status
  // if the data contained in the arguments is not self-consistent.
  grpc::Status SerializeReport(const std::vector<ReportRow>& report_rows,
                               std::string* serialized_report_out,
                               std::string* mime_type_out);

  // Starts the process of serializing the report described by the parameters
  // passed to the constructor. The state of this instance is set up and,
  // depending on the serialization type, a header row may be written to
  // |stream|. After this method finishes the accessor mime_type() may be used
  // to access the MIME type of the report. After this method is invoked the
  // method AppendRows() should be invoked repeatedly in order to cause the
  // rows of the report to be serialized.
  //
  // Returns OK on success. Logs an ERROR and returns a different status
  // on error.
  grpc::Status StartSerializingReport(std::ostream* stream);

  // Continues the process of serializing the report described by the parameters
  // passed to the constructor. The next batch of rows will be read from
  // |row_iterator| and serialized and written to |stream|. The parameter
  // |max_bytes| determines how many rows from |row_iterator| will be read,
  // serialized and written. If, after writing a row, the total number of bytes
  // written by this invocation of AppendRows() is at least |max_bytes|, then
  // this invocation of AppendRows() will exit without reading any more rows
  // from |row_iterator|.
  //
  // The type of the rows returned from |row_iterator| must correspond to the
  // |report_type| from the |metadata| passed to the constructor.
  //
  // Returns OK on success. Logs an ERROR and returns a different status
  // on error.
  grpc::Status AppendRows(size_t max_bytes, ReportRowIterator* row_iterator,
                          std::ostream* stream);

  std::string mime_type() { return mime_type_; }

 private:
  grpc::Status StartSerializingCSVReport(std::ostream* stream);

  grpc::Status AppendCSVRows(size_t max_bytes, ReportRowIterator* row_iterator,
                             std::ostream* stream);

  grpc::Status AppendCSVHeaderRow(std::ostream* stream);

  grpc::Status AppendCSVHistogramHeaderRow(std::ostream* stream);

  grpc::Status AppendCSVRawDumpHeaderRow(std::ostream* stream);

  grpc::Status AppendCSVJointHeaderRow(std::ostream* stream);

  grpc::Status AppendCSVHeaderRowVariableNames(std::ostream* stream);

  grpc::Status AppendCSVReportRow(const ReportRow& report_row,
                                  std::ostream* stream);

  grpc::Status AppendCSVHistogramReportRow(const HistogramReportRow& report_row,
                                           std::ostream* stream);

  grpc::Status AppendCSVRawDumpReportRow(const RawDumpReportRow& report_row,
                                         std::ostream* stream);

  grpc::Status AppendCSVJointReportRow(const JointReportRow& report_row,
                                       std::ostream* stream);

  const ReportConfig* report_config_;        // not owned
  const ReportMetadataLite* metadata_;       // not owned
  const ReportExportConfig* export_config_;  // not owned
  // The parameters below are initialized by StartSerializingReport().
  size_t num_columns_;
  std::vector<std::string> fixed_leftmost_column_values_;
  std::string mime_type_;
};

}  // namespace analyzer
}  // namespace cobalt
#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_SERIALIZER_H_
