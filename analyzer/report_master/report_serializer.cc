// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_serializer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>

#include "glog/logging.h"
#include "util/crypto_util/base64.h"
#include "util/crypto_util/hash.h"
#include "util/datetime_util.h"

namespace cobalt {
namespace analyzer {

using crypto::byte;
using crypto::hash::DIGEST_SIZE;
using crypto::hash::Hash;

namespace {
// The field separator to use for our CSV output.
const char kSeparator[] = ",";

// Due to the nature of the Cobalt encodings, there are cases where a
// generated report contains rows with no information. In those cases it
// is more useful to omit serializing the row. One example is the case in
// which Basic RAPPOR is being used with the INDEX data type. In this case we
// may pre-allocate a large block of indices that are not currently being used
// by the client application. We have therefore not assigned labels for these
// indices and the client application will never encode those indices.
// The Basic RAPPOR analyzer will still generate a row for such an index
// that will likely have a small value for the count field, and in case
// we are using zero statistical noise (i.e. p=0, q=1) the count field will
// be exactly zero. This method implements a heuristic for detecting that
// case: The value is an index, there is no label, the count is close to zero.
bool ShouldSkipRow(const HistogramReportRow& report_row) {
  if (report_row.value().data_case() == ValuePart::kIndexValue &&
      report_row.label().empty() &&
      std::fabs(report_row.count_estimate()) < 0.0001) {
    return true;
  }
  return false;
}

// Produces a value that is appropriate to use for a column header in a CSV
// file, assuming that the input is a metric part name. Metric part names
// are restricted by the regular expression |validMetricPartName| in the
// file //config/config_parser/src/config_parser/project_config.go.
// We reproduce that regular expression here for convenience:
//     ^[a-zA-Z][_a-zA-Z0-9\\- ]+$
// The logic in this function must be kept in sync with that regular expression.
// The column headers produced by this function will:
//     - Contain only letters, numbers, or underscores.
//     - Start with a letter or underscore
//     - Be at most 128 characters long
//
// See comment below before SerializeReportToCSV().
std::string EscapeMetricPartNameForCSVColumHeader(
    const std::string& metric_part_name) {
  size_t size =
      (metric_part_name.size() <= 128 ? metric_part_name.size() : 128);
  std::string out(size, '_');
  for (size_t i = 0; i < size; i++) {
    const char& c = metric_part_name[i];
    if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9')) {
      out[i] = c;
    }
  }
  return out;
}

// Produces a string that is appropriate to use as a non-column-header value
// in a CSV file. The string produced by this function will:
// - be enclosed in double quotes.
// - have all non-printable bytes and all occurrences of the double-quote
//   character (") and all occurrences of the percent character (%) replaced by
//   their URL encoding (i.e. their %hh encoding).
// - have a maximum length of 258.
std::string ToCSVString(const std::string& in) {
  const char* kHexDigits = "0123456789ABCDEF";

  // Truncate at 256. After enclosing in quotes the max length is 258.
  size_t size = (in.size() <= 256 ? in.size() : 256);
  std::string out;
  // Reserve space in the out string. The +2 is for enclosing in quotes.
  out.reserve(size + 2);
  out.push_back('"');
  for (size_t i = 0; i < size; i++) {
    const char& c = in[i];
    if (std::isprint(c) && c != '"' && c != '%') {
      // Append unescaped byte.
      out.push_back(c);
    } else {
      // Append %hh encoding of byte,
      out.push_back('%');
      out.push_back(kHexDigits[(c >> 4) & 0xf]);
      out.push_back(kHexDigits[c & 0xf]);
    }
  }
  out.push_back('"');
  return out;
}

std::string ValueToString(const ValuePart& value) {
  switch (value.data_case()) {
    case ValuePart::kStringValue:
      return ToCSVString(value.string_value());

    case ValuePart::kIntValue:
      return std::to_string(value.int_value());

    case ValuePart::kBlobValue: {
      // Build the Sha256 hash of the blob.
      byte hash_bytes[DIGEST_SIZE];
      const std::string& blob = value.blob_value();
      Hash(reinterpret_cast<const byte*>(&blob[0]), blob.size(), hash_bytes);
      // Return the Base64 encoding of the Sha256 hash of the blob.
      std::string encoded;
      crypto::Base64Encode(hash_bytes, DIGEST_SIZE, &encoded);
      return encoded;
    }

    case ValuePart::kIndexValue: {
      std::ostringstream stream;
      stream << "<index " << value.index_value() << ">";
      return stream.str();
    }

    default:
      return "<Unrecognized value data type>";
  }
}

std::string FloatToString(float x) {
  int sz = std::snprintf(nullptr, 0, "%.3f", x);
  std::vector<char> buf(sz + 1);  // note +1 for null terminator
  std::snprintf(&buf[0], buf.size(), "%.3f", x);
  std::string s = std::string(&buf[0]);
  if (s == "0.000") {
    return "0";
  }
  return s;
}

std::string CountEstimateToString(float count_estimate) {
  // We clip the count estimate to zero. Techniques such as RAPPOR produce
  // unbiased estimates which may be negative. But exporting a report with
  // negative values for the count will likely cause more confusion than
  // its worth.
  double x = std::max(0.0f, count_estimate);
  return FloatToString(x);
}

std::string StdErrToString(float std_err) {
  // It doesn't make sense for the errors to be negative.
  double x = std::max(0.0f, std_err);
  return FloatToString(x);
}

// Returns a human-readable representation of the report config ID.
// Used in forming error messages.
std::string IdString(const ReportConfig& report_config) {
  std::ostringstream stream;
  stream << "(" << report_config.customer_id() << kSeparator
         << report_config.project_id() << kSeparator << report_config.id()
         << ")";
  return stream.str();
}

std::string DayIndexToDateString(uint32_t day_index) {
  util::CalendarDate cd = util::DayIndexToCalendarDate(day_index);
  std::ostringstream stream;
  stream << cd.year << "-" << cd.month << "-" << cd.day_of_month;
  return stream.str();
}

}  // namespace

grpc::Status ReportSerializer::SerializeReport(
    const ReportConfig& report_config, const ReportMetadataLite& metadata,
    const ReportExportConfig& export_config,
    const std::vector<ReportRow>& report_rows,
    std::string* serialized_report_out, std::string* mime_type_out) {
  auto serialization_case = export_config.export_serialization_case();
  switch (serialization_case) {
    case ReportExportConfig::kCsv:
      return SerializeReportToCSV(report_config, metadata, report_rows,
                                  serialized_report_out, mime_type_out);

    case ReportExportConfig::EXPORT_SERIALIZATION_NOT_SET: {
      std::ostringstream stream;
      stream << "Invalid ReportExportConfig: No export_serialization is set. "
                "In ReportConfig "
             << IdString(report_config);
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
    default: {
      std::ostringstream stream;
      stream
          << "Invalid ReportExportConfig: Unrecognized export_serialization: "
          << serialization_case << " In ReportConfig "
          << IdString(report_config);
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
  }
}

// Implementation note: In the current version of Cobalt the CSV files we are
// producing will be saved to Google Cloud Storage and read by a Google
// Data Studio data connector. Consequently we want to ensure that the
// CSV files we produce adhere to the format specified here:
//
// https://support.google.com/datastudio/answer/7511998?hl=en&ref_topic=7332552#file-format
//
// We summarize the salient points:
//
// - Each row must have the same number of columns, even if data is missing for
//   a particular cell in the table
// - Every CSV file in the same folder must have the same format
// - The column separator must be a comma.
// - If there are commas within the actual data in a field, that field must be
//   surrounded by quotes. If your data includes double quotes, you can use a
//   single quote character to surround the field.
// - The first line in your file must be a header row.
// - Field names must be unique, so you can't have duplicate values in your
//   header row.
// - Column names must:
//     - Contain only letters, numbers, or underscores.
//     - Start with a letter or underscore
//     - Be at most 128 characters long
// - Each line in the file must end with a line break.
// - The GCS connector does not support line breaks in your data even if these
//   are escaped by quotes.
//
// These formatting rules will be followed by this method and by the other
// methods below used by this method.
grpc::Status ReportSerializer::SerializeReportToCSV(
    const ReportConfig& report_config, const ReportMetadataLite& metadata,
    const std::vector<ReportRow>& report_rows,
    std::string* serialized_report_out, std::string* mime_type_out) {
  *mime_type_out = "text/csv";
  std::ostringstream stream;
  size_t num_columns;
  std::vector<std::string> fixed_leftmost_column_values;
  auto status = AppendCSVHeaderRow(report_config, metadata, &num_columns,
                                   &fixed_leftmost_column_values, &stream);
  if (!status.ok()) {
    return status;
  }
  for (const ReportRow& row : report_rows) {
    auto status = AppendCSVReportRow(report_config, metadata, row, num_columns,
                                     fixed_leftmost_column_values, &stream);
    if (!status.ok()) {
      return status;
    }
  }
  *serialized_report_out = stream.str();
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVHeaderRow(
    const ReportConfig& report_config, const ReportMetadataLite& metadata,
    size_t* num_columns_out,
    std::vector<std::string>* fixed_leftmost_column_values_out,
    std::ostringstream* stream) {
  switch (metadata.report_type()) {
    case HISTOGRAM:
      return AppendCSVHistogramHeaderRow(
          report_config, metadata, num_columns_out,
          fixed_leftmost_column_values_out, stream);
      break;

    case JOINT:
      return AppendCSVJointHeaderRow(report_config, metadata, num_columns_out,
                                     fixed_leftmost_column_values_out, stream);
      break;

    default: {
      std::ostringstream error_stream;
      error_stream << "Unrecognized report type: " << metadata.report_type();
      std::string message = error_stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
  }
}

grpc::Status ReportSerializer::AppendCSVHistogramHeaderRow(
    const ReportConfig& report_config, const ReportMetadataLite& metadata,
    size_t* num_columns_out,
    std::vector<std::string>* fixed_leftmost_column_values_out,
    std::ostringstream* stream) {
  CHECK(fixed_leftmost_column_values_out);
  fixed_leftmost_column_values_out->clear();
  if (metadata.variable_indices_size() != 1) {
    std::ostringstream error_stream;
    error_stream << "Invalid ReportMetadataLite: Histogram reports always "
                    "analyze exactly one variable but the number of variable "
                    "indices in metadata is "
                 << metadata.variable_indices_size() << ". For ReportConfig "
                 << IdString(report_config);
    std::string message = error_stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::INVALID_ARGUMENT, message);
  }

  fixed_leftmost_column_values_out->push_back(
      DayIndexToDateString(metadata.first_day_index()));
  if (metadata.first_day_index() == metadata.last_day_index()) {
    (*stream) << "date" << kSeparator;
    *num_columns_out = 4u;
  } else {
    (*stream) << "start_date" << kSeparator << "end_date" << kSeparator;
    fixed_leftmost_column_values_out->push_back(
        DayIndexToDateString(metadata.last_day_index()));
    *num_columns_out = 5u;
  }

  auto status =
      AppendCSVHeaderRowVariableNames(report_config, metadata, stream);
  if (!status.ok()) {
    return status;
  }

  // Append the "count" column header.
  (*stream) << kSeparator << "count";

  // Append the "err" column header.
  (*stream) << kSeparator << "err" << std::endl;
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVJointHeaderRow(
    const ReportConfig& report_config, const ReportMetadataLite& metadata,
    size_t* num_columns_out,
    std::vector<std::string>* fixed_leftmost_column_values_out,
    std::ostringstream* stream) {
  std::ostringstream error_stream;
  error_stream << "JOINT reports are not yet implemented. For ReportConfig"
               << IdString(report_config);
  std::string message = error_stream.str();
  LOG(ERROR) << message;
  return grpc::Status(grpc::UNIMPLEMENTED, message);
}

grpc::Status ReportSerializer::AppendCSVHeaderRowVariableNames(
    const ReportConfig& report_config, const ReportMetadataLite& metadata,
    std::ostringstream* stream) {
  CHECK(stream);
  bool first_column = true;
  for (int index : metadata.variable_indices()) {
    if (first_column) {
      first_column = false;
    } else {
      (*stream) << kSeparator;
    }
    if (index >= report_config.variable_size() || index < 0) {
      std::ostringstream error_stream;
      error_stream
          << "Invalid ReportMetadataLite: Variable index out-of-bounds: "
          << index << ". For ReportConfig " << IdString(report_config);
      std::string message = error_stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
    (*stream) << EscapeMetricPartNameForCSVColumHeader(
        report_config.variable(index).metric_part());
  }
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVReportRow(
    const ReportConfig& report_config, const ReportMetadataLite& metadata,
    const ReportRow& report_row, size_t num_columns,
    const std::vector<std::string>& fixed_leftmost_column_values,
    std::ostringstream* stream) {
  auto row_type = report_row.row_type_case();
  auto report_type = metadata.report_type();
  switch (report_type) {
    case HISTOGRAM: {
      if (row_type != ReportRow::kHistogram) {
        std::ostringstream stream;
        stream << "Expecting a HISTOGRAM row but the row_type=" << row_type
               << ". For ReportConfig " << IdString(report_config);
        std::string message = stream.str();
        LOG(ERROR) << message;
        return grpc::Status(grpc::INTERNAL, message);
      }
      return AppendCSVHistogramReportRow(report_row.histogram(), num_columns,
                                         fixed_leftmost_column_values, stream);
      break;
    }

    case JOINT: {
      if (row_type != ReportRow::kJoint) {
        std::ostringstream stream;
        stream << "Expecting a JOINT row but the row_type=" << row_type
               << ". For ReportConfig " << IdString(report_config);
        std::string message = stream.str();
        LOG(ERROR) << message;
        return grpc::Status(grpc::INTERNAL, message);
      }
      return AppendCSVJointReportRow(report_row.joint(), num_columns,
                                     fixed_leftmost_column_values, stream);
      break;
    }

    default: {
      std::ostringstream stream;
      stream << "Unrecognized row_type: " << row_type;
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
  }
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVHistogramReportRow(
    const HistogramReportRow& report_row, size_t num_columns,
    const std::vector<std::string>& fixed_leftmost_column_values,
    std::ostringstream* stream) {
  size_t num_fixed_values = fixed_leftmost_column_values.size();
  if (num_columns != 3 + num_fixed_values) {
    std::ostringstream error_stream;
    error_stream << "Histogram reports always contain 3 columns in addition to "
                    "the fixed leftmost columns but num_columns="
                 << num_columns << " and num_fixed_values=" << num_fixed_values;
    std::string message = error_stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::INTERNAL, message);
  }
  if (ShouldSkipRow(report_row)) {
    return grpc::Status::OK;
  }
  for (const std::string& v : fixed_leftmost_column_values) {
    (*stream) << v << kSeparator;
  }
  if (!report_row.label().empty()) {
    (*stream) << ToCSVString(report_row.label());
  } else {
    (*stream) << ValueToString(report_row.value());
  }
  (*stream) << kSeparator << CountEstimateToString(report_row.count_estimate());
  (*stream) << kSeparator << StdErrToString(report_row.std_error())
            << std::endl;
  return grpc::Status::OK;
}

grpc::Status ReportSerializer::AppendCSVJointReportRow(
    const JointReportRow& report_row, size_t num_columns,
    const std::vector<std::string>& fixed_leftmost_column_values,
    std::ostringstream* stream) {
  return grpc::Status(grpc::UNIMPLEMENTED,
                      "Joint reports are not implemented.");
}

}  // namespace analyzer
}  // namespace cobalt
