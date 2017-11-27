// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_history_cache.h"

#include <memory>
#include <string>
#include <utility>

#include "config/encodings.pb.h"
#include "config/metrics.pb.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "util/clock.h"
#include "util/datetime_util.h"

namespace cobalt {
namespace analyzer {

using store::ReportStore;
using util::MidnightUtcFromDayIndex;
using util::SystemClock;
using util::TimeToDayIndex;

namespace {
// Returns a human-readable respresentation of the report config ID.
// Used in forming error messages.
std::string IdString(const ReportConfig& report_config) {
  std::ostringstream stream;
  stream << "(" << report_config.customer_id() << ","
         << report_config.project_id() << "," << report_config.id() << ")";
  return stream.str();
}

// Builds the keys used in the map query_performed_.
std::string QueryPerformedKey(const ReportConfig& report_config) {
  std::ostringstream stream;
  stream << report_config.customer_id() << ":" << report_config.project_id()
         << ":" << report_config.id();
  return stream.str();
}

// Builds the keys used in the map history_map_.
std::string HistoryMapKey(const ReportConfig& report_config,
                          uint32_t first_day_index, uint32_t last_day_index) {
  std::ostringstream stream;
  stream << report_config.customer_id() << ":" << report_config.project_id()
         << ":" << report_config.id() << ":" << first_day_index << ":"
         << last_day_index;
  return stream.str();
}

// An instance of ReportHistoryCache is constructed with the parameter
// |day_index_lower_bound| that is a lower bound for all day indices that will
// be used in the method calls to that instance. In this function we compute a
// corresponding timestamp that will act as a lower-bound for our scans of the
// ReportStore by that instance.
//
// The rows of the ReportStore are indexed by the timestamp of the *creation
// time* of the records. We are using here the fact that the ReportScheduler
// schedules reports whose first_day_index and last_day_index are less than
// or equal to the *current* day_index, in UTC, when the ReportScheduler runs.
// What this means is that it is possible to put a lower bound on the time
// that a report for a given day_index could possibly have been created. It
// cannot have been created very much prior to midnight UTC of the day with that
// day_index.
int64_t ComputeQueryIntervalStartTimeSeconds(uint32_t day_index_lower_bound) {
  // Just for good measure we return midnight UTC of the *previous day*.
  return MidnightUtcFromDayIndex(day_index_lower_bound - 1);
}
}  // namespace

// These structs are the values of the history_map_.
struct ReportHistoryCache::ReportHistory {
  // Do we already know that there is at least one successfully completed
  // report for this report configuration?
  bool known_completed_successfully = false;

  // If this is not NULL it means that RecordStart() was invoked with this
  // ReportId and we do not yet know that the report with this ID is complete.
  std::unique_ptr<ReportId> report_id_in_progress;
};

ReportHistoryCache::ReportHistoryCache(
    uint32_t day_index_lower_bound,
    std::shared_ptr<store::ReportStore> report_store)
    : query_interval_start_time_seconds_(
          ComputeQueryIntervalStartTimeSeconds(day_index_lower_bound)),
      report_store_(report_store) {}
ReportHistoryCache::~ReportHistoryCache() {}

ReportHistoryCache::ReportHistory* ReportHistoryCache::GetHistory(
    const ReportConfig& report_config, uint32_t first_day_index,
    uint32_t last_day_index) {
  std::unique_ptr<ReportHistory>& value = history_map_[HistoryMapKey(
      report_config, first_day_index, last_day_index)];
  if (!value) {
    value.reset(new ReportHistory());
  }
  return value.get();
}

bool ReportHistoryCache::WasQueryPerformed(const ReportConfig& report_config) {
  return query_performed_.find(QueryPerformedKey(report_config)) !=
         query_performed_.end();
}

void ReportHistoryCache::SetQueryPerformed(const ReportConfig& report_config) {
  query_performed_[QueryPerformedKey(report_config)] = true;
}

void ReportHistoryCache::Refresh(const ReportConfig& report_config,
                                 uint32_t first_day_index,
                                 uint32_t last_day_index) {
  ReportHistory* history =
      GetHistory(report_config, first_day_index, last_day_index);
  if (history->report_id_in_progress) {
    // Since there is a known in-progress report we simply fetch the metadata
    // for it.
    ReportMetadataLite metadata;
    const ReportId& report_id = *(history->report_id_in_progress);
    auto status = report_store_->GetMetadata(report_id, &metadata);
    if (status != store::kOK) {
      LOG(ERROR) << "Unable to GetMetadata for report "
                 << ReportStore::ToString(report_id);
      // Since we are unable to determine if the report is still in progress
      // we'll assume it is.
      return;
    }
    switch (metadata.state()) {
      case WAITING_TO_START:
      case IN_PROGRESS:
        // The report is still in progress
        return;
      case COMPLETED_SUCCESSFULLY:
        history->known_completed_successfully = true;
      // Intentional fall-through.
      case TERMINATED:
        // The report is no longer in-progress.
        history->report_id_in_progress.reset();
        return;
      default:
        LOG(ERROR) << "Unrecognized state for report "
                   << ReportStore::ToString(report_id) << " : "
                   << metadata.state();
        // Since this state is unexpected and possibly unrecoverable we
        // will abandon this in-progress report.
        history->report_id_in_progress.reset();
        return;
    }
  }
  if (WasQueryPerformed(report_config)) {
    return;
  }

  QueryCompletedReports(report_config);

  SetQueryPerformed(report_config);
}

void ReportHistoryCache::QueryCompletedReports(
    const ReportConfig& report_config) {
  std::string pagination_token = "";
  do {
    auto query_reports_response = report_store_->QueryReports(
        report_config.customer_id(), report_config.project_id(),
        report_config.id(), query_interval_start_time_seconds_, UINT64_MAX, 500,
        pagination_token);
    if (query_reports_response.status != store::kOK) {
      LOG(ERROR) << "QueryReports failed for report_config="
                 << IdString(report_config)
                 << ". status=" << query_reports_response.status;
      return;
    }
    for (auto& result : query_reports_response.results) {
      const ReportMetadataLite& metadata = result.report_metadata;
      if (metadata.state() == COMPLETED_SUCCESSFULLY && !metadata.one_off()) {
        ReportHistory* history =
            GetHistory(report_config, metadata.first_day_index(),
                       metadata.last_day_index());
        history->known_completed_successfully = true;
      }
    }
    pagination_token = std::move(query_reports_response.pagination_token);
  } while (!pagination_token.empty());
}

bool ReportHistoryCache::InProgress(const ReportConfig& report_config,
                                    uint32_t first_day_index,
                                    uint32_t last_day_index) {
  ReportHistory* history =
      GetHistory(report_config, first_day_index, last_day_index);
  if (!history->report_id_in_progress) {
    // If RecordStart() wasn't invoked since the last time this report
    // completed then we know the report is not in progress.
    return false;
  }
  // RecordStart() has been called recently. We do a refresh to discover if that
  // insance is completed.
  Refresh(report_config, first_day_index, last_day_index);
  return (history->report_id_in_progress != nullptr);
}

bool ReportHistoryCache::CompletedSuccessfullyOrInProgress(
    const ReportConfig& report_config, uint32_t first_day_index,
    uint32_t last_day_index) {
  ReportHistory* history =
      GetHistory(report_config, first_day_index, last_day_index);
  if (history->known_completed_successfully) {
    return true;
  }

  // Refresh the cache to determine the current state.
  Refresh(report_config, first_day_index, last_day_index);
  return (history->known_completed_successfully ||
          history->report_id_in_progress != nullptr);
}

void ReportHistoryCache::RecordStart(const ReportConfig& report_config,
                                     uint32_t first_day_index,
                                     uint32_t last_day_index,
                                     const ReportId& report_id) {
  ReportHistory* history =
      GetHistory(report_config, first_day_index, last_day_index);
  history->report_id_in_progress.reset(new ReportId(report_id));
}

}  // namespace analyzer
}  // namespace cobalt
