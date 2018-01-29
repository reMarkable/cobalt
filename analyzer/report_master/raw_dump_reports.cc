// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/raw_dump_reports.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "glog/logging.h"
#include "util/log_based_metrics.h"

namespace cobalt {
namespace analyzer {

using config::AnalyzerConfig;
using store::ObservationStore;

// Stackdriver metric constants
namespace {
const char kRawDumpReportError[] = "raw-dump-report-error";
}  // namespace

RawDumpReportRowIterator::RawDumpReportRowIterator(
    uint32_t customer_id, uint32_t project_id, uint32_t metric_id,
    uint32_t start_day_index, uint32_t end_day_index,
    std::vector<std::string> parts, bool include_system_profiles,
    std::string report_id_string,
    std::shared_ptr<store::ObservationStore> observation_store,
    std::shared_ptr<AnalyzerConfig> analyzer_config)
    : customer_id_(customer_id),
      project_id_(project_id),
      metric_id_(metric_id),
      report_id_string_(std::move(report_id_string)),
      start_day_index_(start_day_index),
      end_day_index_(end_day_index),
      parts_(std::move(parts)),
      include_system_profiles_(include_system_profiles),
      observation_store_(observation_store) {
  std::ostringstream stream;
  stream << "(" << customer_id << ", " << project_id << ", " << metric_id
         << ")";
  std::string metric_id_string = stream.str();
  const Metric* metric =
      analyzer_config->Metric(customer_id_, project_id_, metric_id_);
  if (!metric) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kRawDumpReportError)
        << "Metric " << metric_id_string
        << " not found in the the AnalyzerConfig, when initializing a "
           "RawDumpReportRowIterator for report_id="
        << report_id_string_;
    return;
  }
  for (const auto& part_name : parts_) {
    auto it = metric->parts().find(part_name);
    if (it == metric->parts().cend()) {
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kRawDumpReportError)
          << "Metric part '" << part_name << "' not found in Metric "
          << metric_id_string
          << " when initializing a RawDumpReportRowIterator for report_id="
          << report_id_string_;
      return;
    }
    expected_data_types_.push_back(it->second.data_type());
  }
  std::string parts_string;
  if (VLOG_IS_ON(1)) {
    std::ostringstream stream;
    bool first = true;
    for (const std::string& part : parts_) {
      if (first) {
        first = false;
      } else {
        stream << ", ";
      }
      stream << part;
    }
    parts_string.assign(stream.str());
  }
  VLOG(1) << "RawDumpReportRowIterator: Initialized for report_id="
          << report_id_string_ << "with metric " << metric_id_string
          << " day range =[" << start_day_index << ", " << end_day_index
          << "] parts=[" << parts_string << "]";
}

grpc::Status RawDumpReportRowIterator::Reset() {
  have_query_response_ = false;
  have_next_row_ = false;
  eof_ = false;
  result_index_ = -1;
  return grpc::Status::OK;
}

grpc::Status RawDumpReportRowIterator::NextRow(const ReportRow** row) {
  TryEnsureHaveNextRow();
  if (have_next_row_) {
    current_row_.Swap(&next_row_);
    have_next_row_ = false;
    *row = &current_row_;
    return grpc::Status::OK;
  }
  if (query_response_.status != store::kOK) {
    return grpc::Status(grpc::INTERNAL,
                        "QueryObservations() returned error status.");
  }
  return grpc::Status(grpc::NOT_FOUND, "eof");
}

grpc::Status RawDumpReportRowIterator::HasMoreRows(bool* b) {
  CHECK(b);
  TryEnsureHaveNextRow();
  if (have_next_row_) {
    *b = true;
    return grpc::Status::OK;
  }
  if (query_response_.status != store::kOK) {
    return grpc::Status(grpc::INTERNAL,
                        "QueryObservations() returned error status.");
  }
  *b = false;
  return grpc::Status::OK;
}

void RawDumpReportRowIterator::TryEnsureHaveNextRow() {
  ValidateState();
  if (have_next_row_ || eof_) {
    return;
  }
  if (!have_query_response_) {
    QueryObservations("");
  }
  if (query_response_.status != store::kOK) {
    have_next_row_ = false;
    return;
  }
  // Keep trying to build next_row_ until we succeed or reach OEF or encounter
  // a query error. In the case that we encounter an invalid input row (one
  // that cannot be converted to next_row_) then we LOG(ERROR) but keep going.
  while (true) {
    // Advance to the next input result.
    result_index_++;
    // If we have used up all the results in the current query_response_
    // then perform another query.
    if (result_index_ >= static_cast<int>(query_response_.results.size())) {
      if (query_response_.pagination_token.empty()) {
        eof_ = true;
        return;
      }
      QueryObservations(query_response_.pagination_token);
      if (query_response_.status != store::kOK) {
        return;
      }
      if (query_response_.results.empty()) {
        eof_ = true;
        return;
      }
      result_index_ = 0;
    }
    TryBuildNextRow();
    if (have_next_row_) {
      return;
    }
  }
}

void RawDumpReportRowIterator::TryBuildNextRow() {
  have_next_row_ = false;
  if (!have_query_response_ ||
      result_index_ >= static_cast<int>(query_response_.results.size())) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kRawDumpReportError)
        << "Internal logic error. "
           "TryBuildNextRow() invoked with no available input row.";
    return;
  }
  next_row_.Clear();
  RawDumpReportRow* dump = next_row_.mutable_raw_dump();
  Observation& observation = query_response_.results[result_index_].observation;
  size_t part_index = 0;
  for (const auto& part_name : parts_) {
    auto part_iter = observation.parts().find(part_name);
    if (part_iter == observation.parts().cend()) {
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kRawDumpReportError)
          << "Encountered an Observation that was "
             "missing a part while processing a RAW_DUMP report."
          << " For report_id=" << report_id_string_ << ", part=" << part_name;
      return;
    }
    auto observation_part = part_iter->second;
    if (observation_part.value_case() != ObservationPart::kUnencoded) {
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kRawDumpReportError)
          << "Encountered an ObservationPart that "
             "did not use the no-op encoding while processing a "
             "RAW_DUMP report."
          << " For report_id=" << report_id_string_ << ", part=" << part_name
          << ". value_case=" << observation_part.value_case();
      return;
    }
    ValuePart* unencoded_value =
        observation_part.mutable_unencoded()->mutable_unencoded_value();
    MetricPart::DataType value_data_type;
    switch (unencoded_value->data_case()) {
      case ValuePart::kStringValue:
        value_data_type = MetricPart::STRING;
        break;

      case ValuePart::kIntValue:
        value_data_type = MetricPart::INT;
        break;

      case ValuePart::kDoubleValue:
        value_data_type = MetricPart::DOUBLE;
        break;

      case ValuePart::kBlobValue:
        value_data_type = MetricPart::BLOB;
        break;

      case ValuePart::kIndexValue: {
        value_data_type = MetricPart::INDEX;
        break;
      }
      default: {
        LOG_STACKDRIVER_COUNT_METRIC(ERROR, kRawDumpReportError)
            << "Encountered an unrecognized "
               "ValuePart data type while processing a RAW_DUMP report. "
               "For report_id="
            << report_id_string_ << ", part=" << part_name;
        return;
      }
    }
    auto expected_type = expected_data_types_[part_index++];
    if (value_data_type != expected_type) {
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kRawDumpReportError)
          << "Encountered the wrong ValuePart "
             "data type while processing a RAW_DUMP report. For "
             "report_id="
          << report_id_string_ << ", part=" << part_name
          << " expected type=" << expected_type
          << " value type=" << value_data_type;
      return;
    }
    dump->add_values()->Swap(unencoded_value);
  }

  have_next_row_ = true;
}

void RawDumpReportRowIterator::QueryObservations(std::string pagination_token) {
  static const size_t kMaxResultsPerQuery = 1000;
  query_response_ = observation_store_->QueryObservations(
      customer_id_, project_id_, metric_id_, start_day_index_, end_day_index_,
      parts_, include_system_profiles_, kMaxResultsPerQuery, pagination_token);
  if (query_response_.status != store::kOK) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kRawDumpReportError)
        << "QueryObservations() returned error "
           "status: "
        << query_response_.status << ". For report_id=" << report_id_string_;
  }
  have_query_response_ = true;
}

void RawDumpReportRowIterator::ValidateState() {
  bool valid = true;
  if (parts_.empty()) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kRawDumpReportError)
        << "Config for RAW_DUMP report did not specify any variables to dump."
           ". For report_id="
        << report_id_string_;
    valid = false;
  }
  if (expected_data_types_.size() != parts_.size()) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kRawDumpReportError)
        << "Not all of the specified metric parts were found in the Metric "
           "when initializing this RawDumpReportRowIterator for report_id="
        << report_id_string_
        << ". num found parts=" << expected_data_types_.size()
        << ", num expected parts=" << parts_.size();
    valid = false;
  }
  if (!valid) {
    have_next_row_ = false;
    eof_ = false;
    have_query_response_ = true;
    query_response_.status = store::kOperationFailed;
  }
}

}  // namespace analyzer
}  // namespace cobalt
