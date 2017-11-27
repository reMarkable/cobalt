// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_HISTORY_CACHE_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_HISTORY_CACHE_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "analyzer/report_master/report_internal.pb.h"
#include "analyzer/store/report_store.h"
#include "config/analyzer_config.h"

namespace cobalt {
namespace analyzer {

// ReportHistoryCache is used by ReportScheduler to determine the current
// state of report execution for a given report configuration. Let's define
// a |report configuration| to be a triple of the form:
// (ReportConfig, first_day_index, last_day_index). Triples of this form
// act as the indices over which ReportScheduler operates. That is, given
// a triple of this form, ReportScheduler needs to decide whether or not
// a report needs to be executed corresponding to this triple.
// There are two questions that the ReportScheduler needs to ask about a given
// report configuration:
// (i) Is there at least one successfully completed instance of a report for it?
// (ii) Is there currently an ongoing execution of a report for it? We define
//     the notion of an ongoing execution to mean that a report was started
//     during this instantiation of the ReportMaster. In other words if
//     a report was started and then the ReportMaster crashes before the
//     report completes and then the ReportMaster is restarted, then the
//     ReportStore will contain an indication that the report was started
//     but not completed, but the ReportHistoryCache will not consider this to
//     be an ongoing execution. The previous report execution will be
//     abandoned. Only reports that were started during the current running
//     of the ReportMaster count as ongoing reports.
//
// The ReportScheduler queries the ReportHistoryCache for answers to questions
// (i) and (ii) via the methods InProgress() and
// CompletedSuccessfullyOrInProgress(). (The ReportScheduler really wants to
// ask either question (i) or the disjunction of questions (i) and (ii).)
// Furthermore the ReportScheduler notifies the ReportHistoryCache that
// execution of a report instance has begun via the method RecordStart().
//
// The ReportHistoryCache answers questions (i) and (ii) via a combination of
// querying an underlying ReportStore, and keeping an in-memory cache.
// There are two types of queries against the underlying ReportStore that
// are made:
// (a) A scan of all ReportMetadata for a given ReportConfig over a certain
//     time window.
// (b) A fetching of the ReportMetadata for a single
// (ReportConfig, first_day_index, last_day_index) triple.
// The type (a) query is only ever performed once per ReportConfig. After
// that all further questions are answered via queries of type (b) and
// the in-memory cache.
//
// Usage:
// Construct an instance of ReportHistoryCache passing in a lower bound for
// the day indices that will ever be used in a query, and a pointer to the
// ReportStore that should be queried. Then Invoke RecordStart() whenever
// a new report execution begins and invoke InProgress() and
// CompletedSuccessfullyOrInProgress() in order to query the current execution
// state of a report configuration.
class ReportHistoryCache {
 public:
  // Constructor
  // |day_index_lower_bound|. All values for first_day_index and last_day_index
  // in all invocations of the methods on the constructed instance must be
  // breater than or equal to this lower bound or the results are undefined.
  //
  // |report_store|. The underlying ReportStore that the ReportHistoryCache
  // will query.
  ReportHistoryCache(uint32_t day_index_lower_bound,
                     std::shared_ptr<store::ReportStore> report_store);
  ~ReportHistoryCache();

  // Is there currently an in-progress report execution ongoing for the given
  // (report_config, first_day_index, last_day_index) triple. This is defined
  // to mean that RecordStart() was invoked for this triple with some
  // |report_id| and the ReportStore indicates that the report with that
  // |report_id| is not yet complete.
  bool InProgress(const ReportConfig& report_config, uint32_t first_day_index,
                  uint32_t last_day_index);

  // Is it the case that either there is currently an in-progress report
  // execution ongoing for the given
  // (report_config, first_day_index, last_day_index) triple or there is
  // at least one successfully completed report for this triple?
  bool CompletedSuccessfullyOrInProgress(const ReportConfig& report_config,
                                         uint32_t first_day_index,
                                         uint32_t last_day_index);

  // This method informs the ReportCache that a new report execution is
  // starting.
  void RecordStart(const ReportConfig& report_config, uint32_t first_day_index,
                   uint32_t last_day_index, const ReportId& report_id);

 private:
  struct ReportHistory;

  ReportHistory* GetHistory(const ReportConfig& report_config,
                            uint32_t first_day_index, uint32_t last_day_index);

  bool WasQueryPerformed(const ReportConfig& report_config);

  void SetQueryPerformed(const ReportConfig& report_config);

  void Refresh(const ReportConfig& report_config, uint32_t first_day_index,
               uint32_t last_day_index);

  void QueryCompletedReports(const ReportConfig& report_config);

  int64_t query_interval_start_time_seconds_;

  // The keys of the map represent triples of the form
  // (report_config_id, firt_day_index, last_day_index).
  std::unordered_map<std::string, std::unique_ptr<ReportHistory> > history_map_;

  // We only need to perform a full query for a given report config ID one time
  // in the lifetime of this instance. The keys to this map represent
  // report config IDs and an entry in the map indicates the query has been
  // performed.
  std::unordered_map<std::string, bool> query_performed_;

  std::shared_ptr<store::ReportStore> report_store_;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_HISTORY_CACHE_H_
