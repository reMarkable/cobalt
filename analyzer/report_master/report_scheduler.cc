// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_scheduler.h"

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

using config::ReportRegistry;
using util::SystemClock;
using util::TimeToDayIndex;

DEFINE_uint32(daily_report_makeup_days, 30,
              "The number of days in the past that the ReportMaster should "
              "look to find missed scheduled reports to make up. Must be less "
              "than 100 or we will CHECK fail.");

namespace {
// Returns a human-readable respresentation of the report config ID.
// Used in forming error messages.
// TODO(rudominer) This function has been copied multiple times throughout the
// code. We should centralize it in a utility.
std::string IdString(const ReportConfig& report_config) {
  std::ostringstream stream;
  stream << "(" << report_config.customer_id() << ","
         << report_config.project_id() << "," << report_config.id() << ")";
  return stream.str();
}

}  // namespace

ReportScheduler::ReportScheduler(
    std::shared_ptr<config::AnalyzerConfigManager> config_manager,
    std::shared_ptr<store::ReportStore> report_store,
    std::shared_ptr<ReportStarterInterface> report_starter,
    std::chrono::milliseconds sleep_interval)
    : clock_(new SystemClock()),
      config_manager_(config_manager),
      report_starter_(report_starter),
      report_history_(new ReportHistoryCache(
          CurrentDayIndex() - FLAGS_daily_report_makeup_days, report_store)),
      sleep_interval_(sleep_interval),
      shut_down_(false) {
  CHECK_LT(FLAGS_daily_report_makeup_days, 100);
}

ReportScheduler::~ReportScheduler() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    shut_down_ = true;
    if (!scheduler_thread_.joinable()) {
      return;
    }
  }
  scheduler_thread_.join();
}

void ReportScheduler::Start() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::thread t([this] { this->Run(); });
  scheduler_thread_ = std::move(t);
}

void ReportScheduler::Run() {
  while (!shut_down_) {
    Sleep();
    if (shut_down_) {
      return;
    }
    ProcessReports();
  }
}

void ReportScheduler::Sleep() {
  // Note: We invoke the real system clock here, not clock_->now().
  // This is because even in a test we want to use the real system clock to
  // compute wakeup_time because std::condition_varaible::wait_until() always
  // uses the real system clock. A test is able to control the sleep time by
  // setting the value of sleep_interval_.
  auto wakeup_time = std::chrono::system_clock::now() + sleep_interval_;
  VLOG(3) << "ReportScheduler sleeping for " << sleep_interval_.count() << "ms";
  std::unique_lock<std::mutex> lock(mutex_);
  // Sleep until wakeup_time or shut_down_ = true.
  shut_down_notifier_.wait_until(lock, wakeup_time,
                                 [this] { return this->shut_down_.load(); });
}

void ReportScheduler::ProcessReports() {
  uint32_t current_day_index = CurrentDayIndex();
  std::shared_ptr<config::ReportRegistry> report_registry =
      config_manager_->GetCurrent()->report_registry();
  for (const ReportConfig& report_config : *report_registry) {
    if (shut_down_) {
      return;
    }
    ProcessOneReport(report_config, current_day_index);
  }
}

void ReportScheduler::ProcessOneReport(const ReportConfig& report_config,
                                       uint32_t current_day_index) {
  LOG(INFO) << "ReportScheduler processing report_config "
            << IdString(report_config);
  if (!report_config.has_scheduling()) {
    LOG(INFO) << "Skpping report_config " << IdString(report_config)
              << " because it has no SchedulingConfig.";
    return;
  }
  switch (report_config.scheduling().aggregation_epoch_type()) {
    case DAY:
      ProcessDailyReport(report_config, current_day_index);
      return;

    case WEEK:
      ProcessWeeklyReport(report_config, current_day_index);
      return;

    case MONTH:
      ProcessMonthlyReport(report_config, current_day_index);
      return;

    default: {
      LOG(ERROR) << "Unrecognized aggregatoin_epoch_type: "
                 << report_config.scheduling().aggregation_epoch_type()
                 << "In ReportConfig " << IdString(report_config);
      return;
    }
  }
}

void ReportScheduler::ProcessDailyReport(const ReportConfig& report_config,
                                         uint32_t current_day_index) {
  // Look back a number of days equal to the maximum of daily_report_makeup_days
  // and report_finalization_days.
  auto scheduling = report_config.scheduling();
  if (report_config.scheduling().report_finalization_days() > 20) {
    LOG(ERROR) << "Invalid ReportConfig: " << IdString(report_config)
               << " report_finalization_days too large: "
               << report_config.scheduling().report_finalization_days();
    return;
  }

  uint32_t finalization_days = scheduling.report_finalization_days();
  uint32_t lookback_days = FLAGS_daily_report_makeup_days >= finalization_days
                               ? FLAGS_daily_report_makeup_days
                               : finalization_days;
  uint32_t period_start =
      (current_day_index >= lookback_days ? (current_day_index - lookback_days)
                                          : 0u);
  VLOG(4) << "ReportScheduler considering days in the interval ["
          << period_start << ", " << current_day_index << "]";
  for (uint32_t day_index = period_start; day_index <= current_day_index;
       day_index++) {
    if (shut_down_) {
      return;
    }
    if (ShouldStartDailyReportNow(report_config, day_index,
                                  current_day_index)) {
      StartReportNow(report_config, day_index, day_index);
    } else {
      VLOG(4) << "ShouldStartDailyReportNow() returned false for report_config "
              << IdString(report_config) << " day_index=" << day_index
              << " current_day_index=" << current_day_index;
    }
  }
}

void ReportScheduler::ProcessWeeklyReport(const ReportConfig& report_config,
                                          uint32_t current_day_index) {
  LOG(ERROR)
      << "Scheduling of weekly reports is not yet implemented. ReportConfig: "
      << IdString(report_config);
  return;
}

void ReportScheduler::ProcessMonthlyReport(const ReportConfig& report_config,
                                           uint32_t current_day_index) {
  LOG(ERROR)
      << "Scheduling of monthly reports is not yet implemented. ReportConfig: "
      << IdString(report_config);
  return;
}

bool ReportScheduler::ShouldStartDailyReportNow(
    const ReportConfig& report_config, uint32_t day_index,
    uint32_t current_day_index) {
  if (day_index > current_day_index) {
    LOG(ERROR) << "Unexpected condition: " << day_index
               << " = day_index > current_day_index = " << current_day_index
               << " for ReportConfig " << IdString(report_config);
    return false;
  }
  if (day_index > current_day_index -
                      report_config.scheduling().report_finalization_days()) {
    // We want to generate the report repeatedly during the report finalization
    // period, but we don't want to start it again now if we previously started
    // it and that hasn't completed.
    return !report_history_->InProgress(report_config, day_index, day_index);
  }
  // After the report finalization period we only want to run the report once.
  // If it was ever successfully completed don't run it again. Also if we
  // previously started the report and that attempt hasn't finished yet,
  // don't start it again.
  return !(report_history_->CompletedSuccessfullyOrInProgress(
      report_config, day_index, day_index));
}

void ReportScheduler::StartReportNow(const ReportConfig& report_config,
                                     uint32_t first_day_index,
                                     uint32_t last_day_index) {
  const std::string export_name =
      ReportExportName(report_config, first_day_index, last_day_index);
  ReportId report_id;
  LOG(INFO) << "ReportScheduler starting report " << IdString(report_config)
            << " [" << first_day_index << ", " << last_day_index << "]";
  auto status = report_starter_->StartReport(
      report_config, first_day_index, last_day_index, export_name, &report_id);
  if (!status.ok()) {
    LOG(ERROR)
        << "ReportScheduler was unable to start a report for ReportConfig "
        << IdString(report_config) << " first_day_index=" << first_day_index
        << " last_day_index=" << last_day_index
        << " error code=" << status.error_code()
        << " error message=" << status.error_message();
    return;
  }
  report_history_->RecordStart(report_config, first_day_index, last_day_index,
                               report_id);
}

std::string ReportScheduler::ReportExportName(const ReportConfig& report_config,
                                              uint32_t first_day_index,
                                              uint32_t last_day_index) {
  std::ostringstream stream;
  stream << "report_" << report_config.customer_id() << "_"
         << report_config.project_id() << "_" << report_config.id() << "_"
         << first_day_index << "_" << last_day_index;
  return stream.str();
}

uint32_t ReportScheduler::CurrentDayIndex() {
  CHECK(clock_);
  std::time_t current_time =
      std::chrono::system_clock::to_time_t(clock_->now());
  return TimeToDayIndex(current_time, Metric::UTC);
}

ReportStarter::ReportStarter(ReportMasterService* report_master_service)
    : report_master_service_(report_master_service) {}

grpc::Status ReportStarter::StartReport(const ReportConfig& report_config,
                                        uint32_t first_day_index,
                                        uint32_t last_day_index,
                                        const std::string& export_name,
                                        ReportId* report_id_out) {
  StartReportRequest start_request;
  start_request.set_customer_id(report_config.customer_id());
  start_request.set_project_id(report_config.project_id());
  start_request.set_report_config_id(report_config.id());
  start_request.set_first_day_index(first_day_index);
  start_request.set_last_day_index(last_day_index);
  StartReportResponse response_not_used;
  // This is not a one-off report generation. Rather it is scheduled.
  bool one_off = false;
  return report_master_service_->StartReportNoAuth(
      &start_request, one_off, export_name, report_id_out, &response_not_used);
}

}  // namespace analyzer
}  // namespace cobalt
