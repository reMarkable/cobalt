// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ANALYZER_REPORT_MASTER_RAW_DUMP_REPORTS_H_
#define COBALT_ANALYZER_REPORT_MASTER_RAW_DUMP_REPORTS_H_

#include <memory>
#include <string>
#include <vector>

#include "analyzer/report_master/report_row_iterator.h"
#include "analyzer/store/observation_store.h"
#include "config/analyzer_config.h"
#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {

using config::SystemProfileFields;

// An implementation of ReportRowIterator that yields the rows of a RAW_DUMP
// report. Each yielded report row is essentially a copy of a subset of a raw
// unencoded Observation from the Observation Store. A RawDumpReportRowIterator
// wraps a particular query of the Observation store and will incrementally
// fetch additional pages of results for that query from the Observation Store
// as it yields additional rows.
class RawDumpReportRowIterator : public ReportRowIterator {
 public:
  // Constructor.
  // The first seven parameters, (customer_id, project_id, metric_id,
  // start_day_index, end_day_index, parts, included_system_profile_fields) are
  // passed directly to ObservationStore::QueryObservations() and define the
  // query that this iterator wraps. |report_id_string| is used only for log
  // messages. It should be a string that identifies the ReportId that this
  // iterator is in service of. |observation_store| The ObservationStore
  // |analyzer_config| The current version of Cobalt's metric, encoding and
  //                   report configuration.
  RawDumpReportRowIterator(
      uint32_t customer_id, uint32_t project_id, uint32_t metric_id,
      uint32_t start_day_index, uint32_t end_day_index,
      std::vector<std::string> parts,
      const SystemProfileFields& included_system_profile_fields,
      std::string report_id_string_,
      std::shared_ptr<store::ObservationStore> observation_store,
      std::shared_ptr<config::AnalyzerConfig> analyzer_config);

  virtual ~RawDumpReportRowIterator() = default;

  grpc::Status Reset() override;

  grpc::Status NextRow(const ReportRow** row) override;

  grpc::Status HasMoreRows(bool* b) override;

 private:
  // If |have_next_row_| is true this method returns without doing anything.
  // Otherwise this method attempts to ensure that |next_row_| has been
  // populated with the next ReportRow to be returned by this iterator.
  // This method will keep trying to do this until either it succeeds, or
  // a query error occurs, or we reach EOF. In particular if  we encounter an
  // invalid input row (one that cannot be dumped to |next_row_|)
  // then we LOG(ERROR) but continue to iterate through more input rows.
  //
  // After this method completes check |have_next_row_| to see whether or
  // not it succeeded. If |have_next_row_| is false check |eof_| to see if
  // we reached EOF and check query_response_.status to see if a query
  // error occurred. (|have_query_response_| is guaranteed to be true.)
  //
  // In order to find the next good row, this method may perform some
  // combination of incrementing |result_index_|, invoking TryBuildNextRow(),
  // and invoking QueryObservations(), all possibly multiple times.
  void TryEnsureHaveNextRow();

  // Assumptions: |have_query_response_| is true and
  // |result_index_ < query_response_.results.size()|.
  //
  // This method will attempt to build |next_row_| by dumping the Observation at
  // query_respose_.results[result_index_]. Check |have_next_row_| to see
  // if it succeeded.
  //
  // Some reasons why dumping the Observation might fail include:
  // - It is missing one of the parts named in parts_
  // - One of the parts to be dumped was not encoded by the NoOp encoding.
  // - One of the unencoded values to be dumped had the wrong data type
  //   based on the Metric configuration.
  void TryBuildNextRow();

  // Queries the ObservationStore for another batch of Observations using
  // the parameters passed to the constructor and |pagination_token|,
  // and sets |query_response_| equal to the response.
  // Always sets have_query_response_ true. Check query_response_.status
  // for the status of the query.
  void QueryObservations(std::string pagination_token);

  // Validates the parameters passed to the constructor. If validation fails
  // then we LOG(ERROR) and we indicate the failure to the rest of the
  // code in this class by setting have_next_row=false, eof_=false,
  // have_query_response_=true, and
  // query_response_.status = store::kOperationFailed. Thus we treat validation
  // failure as if a query error occurred.
  void ValidateState();

  // The parameters passed to the constructor.
  const uint32_t customer_id_;
  const uint32_t project_id_;
  const uint32_t metric_id_;
  const std::string report_id_string_;
  const uint32_t start_day_index_;
  const uint32_t end_day_index_;
  const std::vector<std::string> parts_;
  const SystemProfileFields included_system_profile_fields_;
  std::shared_ptr<store::ObservationStore> observation_store_;
  // The data types of the metric parts from the Metric configuration,
  // in the order specified by parts_. We expect each input Observation
  // to have parts with the right names and these data types.
  std::vector<MetricPart::DataType> expected_data_types_;

  // The state of this iterator

  // Indicates whether or not query_response_ was populated via
  // QueryObservations().
  bool have_query_response_ = false;
  store::ObservationStore::QueryResponse query_response_;

  // If have_query_response_ is true and eof_ is false and
  // query_response_.status = kOK, then this is the index
  // into the vector |query_response_.results| of the Observation that was
  // dumped to |next_row_| via TryBuildNextRow(), or this is -1 if
  // query_response_.results[0] has not yet been dumped.
  int result_index_ = -1;

  // Indicates that EOF has already been reached for this iterator.
  bool eof_ = false;

  // Indicates that |next_row_| has been succesfully populated with the
  // dump of of query_response_.results[result_index_] and is ready to
  // be returned via NextRow().
  bool have_next_row_ = false;
  ReportRow next_row_;

  // Holds the ReportRow that is pointed to by the most recent return value
  // of NextRow().
  ReportRow current_row_;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_RAW_DUMP_REPORTS_H_
