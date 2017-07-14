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

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/report_master/report_internal.pb.h"
#include "analyzer/report_master/report_master.pb.h"
#include "analyzer/store/data_store.h"
#include "glog/logging.h"
#include "util/crypto_util/random.h"

using google::protobuf::MessageLite;

namespace cobalt {
namespace analyzer {
namespace store {

namespace {
// We currently do not support reports with more than this many rows.
// TODO(rudominer) Consider supporting arbitrarily large reports. Currently
// we assume all reports fit in memory.
size_t kMaxReportRows = 5000;

// The name of the data column in the report_metadata table
const char kMetadataColumnName[] = "metadata";

// The name of the data column in the report_rows table
const char kReportRowColumnName[] = "report_row";

uint32_t RandomUint32() {
  cobalt::crypto::Random rand;
  return rand.RandomUint32();
}

void ParseReportIdFromMetadataRowKey(const std::string row_key,
                                     ReportId* report_id) {
  int32_t customer_id, project_id, report_config_id, instance_id;
  int64_t creation_time_seconds;
  int sequence_num;
  CHECK_GT(row_key.size(), 68u);
  std::sscanf(&row_key[0], "%10u", &customer_id);
  std::sscanf(&row_key[11], "%10u", &project_id);
  std::sscanf(&row_key[22], "%10u", &report_config_id);
  std::sscanf(&row_key[33], "%20lu", &creation_time_seconds);
  std::sscanf(&row_key[54], "%10u", &instance_id);
  std::sscanf(&row_key[65], "%4u", &sequence_num);
  report_id->set_customer_id(customer_id);
  report_id->set_project_id(project_id);
  report_id->set_report_config_id(report_config_id);
  report_id->set_creation_time_seconds(creation_time_seconds);
  report_id->set_sequence_num(sequence_num);
  report_id->set_instance_id(instance_id);
}

// Parses a protocol buffer message from the bytes in a column of a row.
//
// |report_id| The ReportId from which the data was queried.
//
// |row| The row containing the column. It must have exactly one column.
//
// |column_name| The name of the column within the row containing the data.
//     It must be the name of the unique column within the row.
//
// |error_messge_prefix| If any of the steps fail a LOG(ERROR) message will
//     formed using this as a prefix.
//
// |proto_message| the message into which to parse the data from the column.
Status ParseSingleColumn(const ReportId& report_id, const DataStore::Row& row,
                         const std::string& column_name,
                         const std::string& error_message_prefix,
                         MessageLite* proto_message) {
  if (row.column_values.size() != 1) {
    LOG(ERROR) << error_message_prefix << " for report_id "
               << ReportStore::ToString(report_id)
               << ": expected to receive one column but recieved "
               << row.column_values.size() << " columns.";
    return kOperationFailed;
  }

  auto iter = row.column_values.find(column_name);

  if (iter == row.column_values.end()) {
    LOG(ERROR) << error_message_prefix << " for report_id "
               << ReportStore::ToString(report_id)
               << ": Column not found: " << column_name;
    return kOperationFailed;
  }

  if (!proto_message->ParseFromString(iter->second)) {
    LOG(ERROR) << error_message_prefix << " for report_id "
               << ReportStore::ToString(report_id)
               << ": Unable to parse ReportRow";
    return kOperationFailed;
  }
  return kOK;
}

std::string MakeReportRowKey(const ReportId& report_id, uint32_t suffix) {
  // TODO(rudominer): Replace human-readable row key with smaller more efficient
  // representation.
  std::ostringstream stream;
  stream << ReportStore::ToString(report_id) << ":" << suffix;
  return stream.str();
}

// Returns the common prefix for all row keys in both the ReportRows table
// and the ReportMetadata table corresponding to the given report config.
std::string RowKeyPrefix(uint32_t customer_id, uint32_t project_id,
                         uint32_t report_config_id) {
  // TODO(rudominer) This length corresponds to our current, temporary,
  // human-readable row-keys. This function needs to change when the
  // implementation changes. The prefix we return includes three ten-digit
  // numbers plus three colons.
  static const size_t kPrefixLength = 33;
  ReportId report_id;
  report_id.set_customer_id(customer_id);
  report_id.set_project_id(project_id);
  report_id.set_report_config_id(report_config_id);
  report_id.set_creation_time_seconds(0);
  report_id.set_instance_id(0);
  std::string row_key = ReportStore::ToString(report_id);
  row_key.resize(kPrefixLength);
  return row_key;
}

// Checks that the type of Row contained in |report_row| matches the type of
// report specified by the |report_type| field of |metadata|.
bool CheckRowType(const ReportId& report_id, const ReportMetadataLite& metadata,
                  const ReportRow& report_row) {
  switch (metadata.report_type()) {
    case HISTOGRAM:
      return report_row.has_histogram();
    case JOINT: {
      return report_row.has_joint();
    }
    default: {
      LOG(ERROR) << "Unrecognized ReportType: " << metadata.report_type()
                 << " for report_id=" << ReportStore::ToString(report_id);
      return false;
    }
  }
}

}  // namespace

ReportStore::ReportStore(std::shared_ptr<DataStore> store)
    : store_(store), clock_(new util::SystemClock()) {}

DataStore::Row ReportStore::MakeDataStoreRow(
    const ReportId& report_id, const ReportMetadataLite& metadata) {
  std::string serialized_metadata;
  metadata.SerializeToString(&serialized_metadata);

  // Build a Row
  DataStore::Row row;
  row.key = MakeMetadataRowKey(report_id);
  row.column_values[kMetadataColumnName] = std::move(serialized_metadata);

  return row;
}

Status ReportStore::WriteMetadata(const ReportId& report_id,
                                  const ReportMetadataLite& metadata) {
  auto row = MakeDataStoreRow(report_id, metadata);

  // Write the Row to the report_metadata table.
  Status status = store_->WriteRow(DataStore::kReportMetadata, std::move(row));
  if (status != kOK) {
    LOG(ERROR) << "Error while writing metadata for report_id "
               << ToString(report_id) << ": WriteRow() "
               << "failed with status=" << status;
    return status;
  }

  return kOK;
}

Status ReportStore::WriteBulkMetadata(
    const std::vector<ReportId>& report_ids,
    const std::vector<ReportMetadataLite>& metadata) {
  size_t num_reports = report_ids.size();
  CHECK_EQ(num_reports, metadata.size());
  std::vector<DataStore::Row> rows;
  for (size_t i = 0; i < num_reports; i++) {
    rows.emplace_back(MakeDataStoreRow(report_ids[i], metadata[i]));
  }

  Status status =
      store_->WriteRows(DataStore::kReportMetadata, std::move(rows));
  if (status != kOK) {
    LOG(ERROR) << "Error while writing metadata for " << num_reports
               << "reports: WriteRows() "
               << "failed with status=" << status;
    return status;
  }

  return kOK;
}

Status ReportStore::StartNewReport(
    uint32_t first_day_index, uint32_t last_day_index, bool one_off,
    ReportType report_type, const std::vector<uint32_t>& variable_indices,
    ReportId* report_id) {
  CHECK(report_id);
  // Complete the report_id.
  report_id->set_creation_time_seconds(clock_->CurrentTimeSeconds());
  report_id->set_instance_id(RandomUint32());

  // Build a serialized ReportMetadataLite.
  ReportMetadataLite metadata;
  metadata.set_state(IN_PROGRESS);
  metadata.set_first_day_index(first_day_index);
  metadata.set_last_day_index(last_day_index);
  metadata.set_report_type(report_type);
  for (auto index : variable_indices) {
    metadata.add_variable_indices(index);
  }
  metadata.set_one_off(one_off);
  // We are not just creating but also starting this report now.
  metadata.set_start_time_seconds(report_id->creation_time_seconds());

  return WriteMetadata(*report_id, metadata);
}

Status ReportStore::CreateDependentReport(
    uint32_t sequence_number, ReportType report_type,
    const std::vector<uint32_t>& variable_indices, ReportId* report_id) {
  ReportMetadataLite metadata;
  Status status = GetMetadata(*report_id, &metadata);
  if (status != kOK) {
    return status;
  }

  report_id->set_sequence_num(sequence_number);
  status = GetMetadata(*report_id, &metadata);
  if (status != kNotFound) {
    return kAlreadyExists;
  }

  // Set the state to WAITING_TO_START
  metadata.set_state(WAITING_TO_START);

  // Set the report_type and variable_indices
  metadata.set_report_type(report_type);
  metadata.clear_variable_indices();
  for (auto index : variable_indices) {
    metadata.add_variable_indices(index);
  }

  // Reset the other fields we don't want to copy from the fetched
  // ReportMetadataLite.
  metadata.mutable_info_messages()->Clear();
  // This secondary report is being created but not started.
  metadata.set_start_time_seconds(0);
  metadata.set_finish_time_seconds(0);

  return WriteMetadata(*report_id, metadata);
}

Status ReportStore::StartDependentReport(const ReportId& report_id) {
  ReportMetadataLite metadata;
  Status status = GetMetadata(report_id, &metadata);
  if (status != kOK) {
    return status;
  }
  if (metadata.state() != WAITING_TO_START) {
    return kPreconditionFailed;
  }
  metadata.set_state(IN_PROGRESS);

  // Set the start time to the current time.
  metadata.set_start_time_seconds(clock_->CurrentTimeSeconds());

  return WriteMetadata(report_id, metadata);
}

Status ReportStore::EndReport(const ReportId& report_id, bool success,
                              std::string message) {
  ReportMetadataLite metadata;
  Status status = GetMetadata(report_id, &metadata);
  if (status != kOK) {
    return status;
  }

  metadata.set_finish_time_seconds(clock_->CurrentTimeSeconds());
  metadata.set_state(success ? COMPLETED_SUCCESSFULLY : TERMINATED);

  if (!message.empty()) {
    auto* info_message = metadata.add_info_messages();
    info_message->mutable_timestamp()->set_seconds(
        clock_->CurrentTimeSeconds());
    info_message->set_message(message);
  }

  return WriteMetadata(report_id, metadata);
}

Status ReportStore::AddReportRows(const ReportId& report_id,
                                  const std::vector<ReportRow>& report_rows) {
  if (report_id.creation_time_seconds() == 0 || report_id.instance_id() == 0) {
    LOG(ERROR) << "Attempt to AddReportRow for incomplete report_id: "
               << ToString(report_id);
    return kInvalidArguments;
  }

  ReportMetadataLite metadata;
  Status status = GetMetadata(report_id, &metadata);
  if (status != kOK) {
    LOG(ERROR) << "Failed to get metadata for report_id: "
               << ToString(report_id);
    return status;
  }

  if (metadata.state() != IN_PROGRESS) {
    LOG(ERROR) << "Report is not IN_PROGRESS. state=" << metadata.state()
               << " report_id: " << ToString(report_id);
    return kPreconditionFailed;
  }

  std::vector<DataStore::Row> data_store_rows;

  for (const auto& report_row : report_rows) {
    if (!CheckRowType(report_id, metadata, report_row)) {
      return kInvalidArguments;
    }

    std::string serialized_row;
    if (!report_row.SerializeToString(&serialized_row)) {
      LOG(ERROR) << "Serializing report_row failed";
      return kOperationFailed;
    }

    // Add a new DataStore::Row
    data_store_rows.emplace_back();
    DataStore::Row& row = data_store_rows.back();
    row.key = GenerateReportRowKey(report_id);
    row.column_values[kReportRowColumnName] = std::move(serialized_row);
  }

  // Write the Row to the report_rows table.
  status =
      store_->WriteRows(DataStore::kReportRows, std::move(data_store_rows));
  if (status != kOK) {
    LOG(ERROR) << "Error while attempting to write report rows for report_id "
               << ToString(report_id) << ": WriteRow() "
               << "failed with status=" << status;
    return status;
  }

  return kOK;
}

Status ReportStore::GetMetadata(const ReportId& report_id,
                                ReportMetadataLite* metadata_out) {
  DataStore::Row row;
  row.key = MakeMetadataRowKey(report_id);
  std::vector<std::string> column_names;
  auto status = store_->ReadRow(DataStore::kReportMetadata, column_names, &row);
  if (status != kOK) {
    // Don't LOG(ERROR) here because we use this method to ensure that
    // a report_id does not exist and so we expect kNotFound sometimes.
    VLOG(3) << "Error while attempting to get metadata for report_id "
            << ToString(report_id) << ": ReadRow() "
            << "failed with status=" << status;
    return status;
  }

  return ParseSingleColumn(report_id, row, kMetadataColumnName,
                           "Error while attempting to get metadata",
                           metadata_out);
}

// Note(rudominer) For now we assume a report always fits in memory.
Status ReportStore::GetReport(const ReportId& report_id,
                              ReportMetadataLite* metadata_out,
                              ReportRows* report_out) {
  CHECK(metadata_out);
  CHECK(report_out);
  // Read the ReportMetaData.
  Status status = GetMetadata(report_id, metadata_out);
  if (status != kOK) {
    return status;
  }

  // Read the rows of the report.
  // TODO(rudominer) We really want to read an interval that is closed on the
  // right, but that function is not currently available in the DataStore api.
  std::vector<std::string> column_names;
  auto read_response = store_->ReadRows(
      DataStore::kReportRows, ReportStartRowKey(report_id), true,
      ReportEndRowKey(report_id), std::move(column_names), kMaxReportRows);

  if (read_response.status != kOK) {
    return read_response.status;
  }

  if (read_response.more_available) {
    LOG(ERROR) << "Report contains too many rows to return! "
               << ToString(report_id);
    return kPreconditionFailed;
  }

  // Iterate through the returned DataStore rows. For each returned row...
  for (const DataStore::Row& row : read_response.rows) {
    // parse the ReportRow and add it to report_out.
    auto status =
        ParseSingleColumn(report_id, row, kReportRowColumnName,
                          "Error while reading rows", report_out->add_rows());
    if (status != kOK) {
      return status;
    }
  }
  return kOK;
}

ReportStore::QueryReportsResponse ReportStore::QueryReports(
    uint32_t customer_id, uint32_t project_id, uint32_t report_config_id,
    int64_t interval_start_time_seconds, int64_t interval_end_time_seconds,
    size_t max_results, std::string pagination_token) {
  QueryReportsResponse query_response;

  std::string start_row;
  bool inclusive = true;
  std::string range_start_key = MetadataRangeStartKey(
      customer_id, project_id, report_config_id, interval_start_time_seconds);
  if (!pagination_token.empty()) {
    // The pagination token should be the row key of the last row returned the
    // previous time this method was invoked.
    if (pagination_token < range_start_key) {
      query_response.status = kInvalidArguments;
      return query_response;
    }
    start_row.swap(pagination_token);
    inclusive = false;
  } else {
    start_row.swap(range_start_key);
  }

  std::string limit_row = MetadataRangeStartKey(
      customer_id, project_id, report_config_id, interval_end_time_seconds);

  if (limit_row <= start_row) {
    query_response.status = kInvalidArguments;
    return query_response;
  }

  DataStore::ReadResponse read_response = store_->ReadRows(
      DataStore::kReportMetadata, std::move(start_row), inclusive,
      std::move(limit_row), std::vector<std::string>(), max_results);

  query_response.status = read_response.status;
  if (query_response.status != kOK) {
    return query_response;
  }

  for (const DataStore::Row& row : read_response.rows) {
    // For each row of the read_response we add a ReportRecord to the
    // query_response.
    query_response.results.emplace_back();
    auto& report_record = query_response.results.back();
    ParseReportIdFromMetadataRowKey(row.key, &report_record.report_id);

    auto status = ParseSingleColumn(
        report_record.report_id, row, kMetadataColumnName,
        "Error while querying reports", &report_record.report_metadata);
    if (status != kOK) {
      query_response.status = status;
      return query_response;
    }
  }

  if (read_response.more_available) {
    // If the underling store says that there are more rows available, then
    // we return the row key of the last row as the pagination_token.
    if (read_response.rows.empty()) {
      // There Read operation indicated that there were more rows available yet
      // it did not return even one row. In this pathological case we return
      // an error.
      query_response.status = kOperationFailed;
      return query_response;
    }
    size_t last_index = read_response.rows.size() - 1;
    query_response.pagination_token.swap(read_response.rows[last_index].key);
  }

  return query_response;
}

std::string ReportStore::MakeMetadataRowKey(const ReportId& report_id) {
  // TODO(rudominer): Replace human-readable row key with smaller more efficient
  // representation.
  return ToString(report_id);
}

std::string ReportStore::MetadataRangeStartKey(uint32_t customer_id,
                                               uint32_t project_id,
                                               uint32_t report_config_id,
                                               int64_t creation_time_seconds) {
  ReportId report_id;
  report_id.set_customer_id(customer_id);
  report_id.set_project_id(project_id);
  report_id.set_report_config_id(report_config_id);
  report_id.set_creation_time_seconds(creation_time_seconds);
  report_id.set_instance_id(0);
  // Leave sequence_num unset because the default value is zero.
  return MakeMetadataRowKey(report_id);
}

std::string ReportStore::GenerateReportRowKey(const ReportId& report_id) {
  return MakeReportRowKey(report_id, RandomUint32());
}

std::string ReportStore::ReportStartRowKey(const ReportId& report_id) {
  // TODO(rudominer): Replace human-readable row key with smaller more efficient
  // representation.
  std::ostringstream stream;
  stream << ToString(report_id) << ":";
  return stream.str();
}

std::string ReportStore::ReportEndRowKey(const ReportId& report_id) {
  // TODO(rudominer): Replace human-readable row key with smaller more efficient
  // representation.
  std::ostringstream stream;
  stream << ToString(report_id) << ":9999999999";
  return stream.str();
}

std::string ReportStore::ToString(const ReportId& report_id) {
  // We write four ten-digit numbers, plus one twenty-digit number plus one
  // four digit number plus five coluns. That is 69 characters. The string has
  // size 70 to accommodate a trailing null character.
  std::string out(70, 0);

  std::snprintf(&out[0], out.size(), "%.10u:%.10u:%.10u:%.20lu:%.10u:%.4u",
                report_id.customer_id(), report_id.project_id(),
                report_id.report_config_id(), report_id.creation_time_seconds(),
                report_id.instance_id(), report_id.sequence_num());

  // Discard the trailing null character.
  out.resize(69);

  return out;
}

Status ReportStore::DeleteAllForReportConfig(uint32_t customer_id,
                                             uint32_t project_id,
                                             uint32_t report_config_id) {
  auto status = store_->DeleteRowsWithPrefix(
      DataStore::kReportMetadata,
      RowKeyPrefix(customer_id, project_id, report_config_id));
  if (status != kOK) {
    return status;
  }
  return store_->DeleteRowsWithPrefix(
      DataStore::DataStore::kReportRows,
      RowKeyPrefix(customer_id, project_id, report_config_id));
}

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
