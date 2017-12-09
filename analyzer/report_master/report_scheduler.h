// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_SCHEDULER_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_SCHEDULER_H_

#include <atomic>
#include <memory>
#include <string>

#include "analyzer/report_master/report_history_cache.h"
#include "analyzer/report_master/report_master_service.h"
#include "analyzer/store/report_store.h"
#include "config/report_config.h"
#include "util/clock.h"

namespace cobalt {
namespace analyzer {

// An abstract interface that allows the real ReportMasterService to be
// mocked out in unit tests of the ReportScheduler.
class ReportStarterInterface {
 public:
  virtual grpc::Status StartReport(const ReportConfig& report_config,
                                   uint32_t first_day_index,
                                   uint32_t last_day_index,
                                   const std::string& export_name,
                                   ReportId* report_id_out) = 0;

  virtual ~ReportStarterInterface() = default;
};

// Forward declare ReportMasterService because report_master_serivce.h and
// report_scheduler.h include each other.
class ReportMasterService;

// An implementation of ReportStarterInterface that delegates to an instance
// of ReportMasterService. This is the implementation used in production.
class ReportStarter : public ReportStarterInterface {
 public:
  explicit ReportStarter(ReportMasterService* report_master_service);
  virtual ~ReportStarter() = default;
  grpc::Status StartReport(const ReportConfig& report_config,
                           uint32_t first_day_index, uint32_t last_day_index,
                           const std::string& export_name,
                           ReportId* report_id_out) override;

 private:
  ReportMasterService* report_master_service_;  // not owned
};

// ReportScheduler periodically runs reports according to their configured
// schedules.
//
// A ReportConfig contains a ReportSchedulingConfig that contains two fields
// that influence report scheduling:
// |aggreggation_epoch_type| and |report_finalization_days|.
//
// There are three aggregation epoch types: DAY, WEEK and MONTH. The DAY type
// means that each report aggregates the set of Observations from a single day,
// and that the report is run daily. Since WEEK and MONTH reports are not
// currently implemented, the remainder of this description will assume that the
// aggregation epoch type is DAY.
//
// Each Observation sent from an Encoder client is tagged with a |day_index|
// indicating which day the Observation corresponds to. The |day_index|
// is computed based on a time zone specified in the MetricConfig--it is not
// necessarily the local time zone of the Encoder client. The ReportScheduler
// running within the ReportMaster always uses the UTC time zone to compute
// the current day index at report generation time.
//
// The |report_finalization_days| field of a ReportConfig indicates how many
// days to wait for Observations to arrive before considering a report
// finalized. The ReportScheduler will regenerate a report multiple times
// to allow  additional observations to trickle in up to several days after the
// report period ends. This is important for several reasons: (a) The client
// and server may use different time zones (b) The client may be temporarily
// offline (c) the Shuffler may be configured to intentionally add a delay.
// |report_finalization_days| controls the number after days after the
// report day before the ReportScheduler considers the report to be finalized.
//
// The FLAG |daily_report_makeup_days| is an important parameter in the
// scheduling algorithm. This is the number of days in the past that the
// ReportScheduler will look to find instances of reports that should have been
// executed but were not. By default its value is 30.
//
// The scheduling algorithm is as follows. A single background thread called
// the scheduler thread loops forever, sleeping for a while
// (by default 17 minutes) and then waking up and iteratively processing all of
// the registered report configurations. Processing of a single report
// configuration X of Day type proceeds as follows. First the current day index
// is computed relative to the UTC timezone. Next we consider the interval
// of day indices [a, b] where b = the current day index and where
// a = b - FLAGS_daily_report_makeup_days. For each day index d in [a, b]
// we ask if we should run a report for report config X for day d and if the
// answer is yes then we do run such a report. The way we determine whether
// or not we should run a report depends on whether or not report config X
// for day d has already been finalized. If d <= b - report_finalization_days
// then the report is considered finalized. In this case we will only run a
// report for day d if there is not already a successfully completed report
// for day d. Also we keep track of whether or not we have already started
// a report for report config X for day d that has not yet finished running.
// In this case also we will not start another one. If
// instead d > b - report_finalization_days then the report for report config
// X for day d is considered not yet finalized. In this case we will always
// run another such report unless one was started earlier and is not yet
// finished running.
//
// Usage: Construct a ReportScheduler and then invoke Start(), which returns
// immediately. ReportScheduler has a background scheduler thread that runs
// until the instance of ReportScheduler is destructed.
class ReportScheduler {
 public:
  // |report_registry| contains the registered ReportConfigs. This determines
  // which reports to run and their schedules. This data is "live": The
  // registered ReportConfigs are re-read periodically (based on the parameter
  // |sleep_interval|).
  //
  // |report_store| is used to query the history of generated reports in order
  // to determine whether a report needs to be run.
  //
  // |report_starter| is used to start the asynchronous generation of reports.
  //
  // |sleep_interval| determines the frequency with which ReportScheduler
  // re-reads the registered reports in |analyzer_config| and checks
  // to see if it is time to generate a report. Optional, defaults to 17
  // minutes.
  ReportScheduler(
      std::shared_ptr<config::AnalyzerConfigManager> config_manager,
      std::shared_ptr<store::ReportStore> report_store,
      std::shared_ptr<ReportStarterInterface> report_starter,
      std::chrono::milliseconds sleep_interval =
          std::chrono::milliseconds(1000 * 60 * 17));

  // The destructor will stop the scheduler thread and wait for it to stop
  // before exiting.
  ~ReportScheduler();

  // Starts the scheduler thread. Destruct this object to stop the thread.
  // This method must be invoked exactly once.
  void Start();

  void SetClockForTesting(std::shared_ptr<util::ClockInterface> clock) {
    clock_ = clock;
  }

 private:
  friend class ReportSchedulerTest;

  // The main function that runs in the ReportScheduler's scheduler thread.
  // Loops forever, repeatedly invoking Sleep() and ProcessReports() until
  // shut_down_ is set true.
  void Run();

  // Sleeps for |sleep_interval_| time, or until |shut_down_| is set true.
  void Sleep();

  // Returns the current day index relative to UTC at the current time.
  uint32_t CurrentDayIndex();

  // Iterates through all of the registered report configs, invoking
  // ProcessOneReport() on each.
  void ProcessReports();

  // Invokes ProcessDailyReport, ProcessWeeklyReport or ProcessMonthlyReport
  // as appropriate.
  void ProcessOneReport(const ReportConfig& report_config,
                        uint32_t current_day_index);

  // Process one daily report. For each day over the previous 31 days
  // (this may be overridden by FLAGS_daily_report_makeup_days), invokes
  // ShouldStartDailyReportNow() and if that method returns true then invokes
  // StartReportNow().
  void ProcessDailyReport(const ReportConfig& report_config,
                          uint32_t current_day_index);

  // Not currently implemented.
  void ProcessWeeklyReport(const ReportConfig& report_config,
                           uint32_t current_day_index);
  // Not currently implemented.
  void ProcessMonthlyReport(const ReportConfig& report_config,
                            uint32_t current_day_index);

  // Determines if a report for the given ReportConfig should be run
  // for the given day_index assume the current day index is given
  // by |current_day_index|.
  bool ShouldStartDailyReportNow(const ReportConfig& report_config,
                                 uint32_t day_index,
                                 uint32_t current_day_index);

  // Uses the ReportStarter passed in to the constructor to start the
  // specified report for the specified interval of days.
  void StartReportNow(const ReportConfig& report_config,
                      uint32_t first_day_index, uint32_t last_day_index);

  // Generates the name by which the report with the specified parameters
  // should be exported.
  std::string ReportExportName(const ReportConfig& report_config,
                               uint32_t first_day_index,
                               uint32_t last_day_index);

  // The clock is abstracted so that friend tests can set a non-system clock.
  std::shared_ptr<util::ClockInterface> clock_;

  // The "Run()" method runs in this thread.
  std::thread scheduler_thread_;

  std::shared_ptr<config::AnalyzerConfigManager> config_manager_;
  std::shared_ptr<ReportStarterInterface> report_starter_;
  std::unique_ptr<ReportHistoryCache> report_history_;

  // How much time to sleep during the Sleep() method.
  std::chrono::milliseconds sleep_interval_;

  // Protects  shut_down_ and shut_down_notifier_
  std::mutex mutex_;

  std::atomic<bool> shut_down_;

  std::condition_variable shut_down_notifier_;  // Protected by mutex_.
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_SCHEDULER_H_
