// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_scheduler.h"

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/report_master/report_master_service.h"
#include "analyzer/store/memory_store.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/datetime_util.h"

namespace cobalt {
namespace analyzer {

// This flag controls the number of days in the past that the ReportScheduler
// will look for reports that were supposed to be run but were not.
DECLARE_uint32(daily_report_makeup_days);

using config::ReportRegistry;
using store::DataStore;
using store::MemoryStore;
using store::ReportStore;
using util::IncrementingClock;

namespace {
const uint32_t kFirstDayIndex = 12345;
const uint32_t kStartingTimeSeconds =
    kFirstDayIndex * util::kNumUnixSecondsPerDay;
const uint32_t kTenMinutes = 600;

const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;
const uint32_t kReportConfigId = 42;
const uint32_t kReportConfigId2 = 43;
const uint32_t kReportConfigId3 = 44;
const uint32_t kReportConfigId4 = 45;
// "report_finalization_days" is the new name for the field whose current
// name is "report_delay_days" in the ReportConfig proto message.
const uint32_t kReportFinalizationDays = 3;
const uint32_t kReportFinalizationDays2 = 2;
const uint32_t kReportFinalizationDays3 = 1;
const uint32_t kReportFinalizationDays4 = 0;
const char* kReportConfigText = R"(
element {
  customer_id: 1
  project_id: 1
  id: 42
  metric_id: 1
  report_type: HISTOGRAM
  report_delay_days: 3
  aggregation_epoch_type: DAY
}

element {
  customer_id: 1
  project_id: 1
  id: 43
  metric_id: 1
  report_type: HISTOGRAM
  report_delay_days: 2
  aggregation_epoch_type: DAY
}

element {
  customer_id: 1
  project_id: 1
  id: 44
  metric_id: 1
  report_type: HISTOGRAM
  report_delay_days: 1
  aggregation_epoch_type: DAY
}

element {
  customer_id: 1
  project_id: 1
  id: 45
  metric_id: 1
  report_type: HISTOGRAM
  report_delay_days: 0
  aggregation_epoch_type: DAY
}

)";
}  // namespace

// An implementation of ReportStarterInterface that registers reports in
// the ReportStore as started (and optionally as completed) but does not
// actually run any reports. It also records the values of all of the
// parameters it was invoked with for checking by a test.
class FakeReportStarter : public ReportStarterInterface {
 public:
  explicit FakeReportStarter(std::shared_ptr<ReportStore> report_store)
      : report_store_(report_store) {}
  virtual ~FakeReportStarter() = default;

  grpc::Status StartReport(const ReportConfig& report_config,
                           uint32_t first_day_index, uint32_t last_day_index,
                           const std::string& export_name,
                           ReportId* report_id_out) override {
    report_id_out->Clear();
    report_id_out->set_customer_id(kCustomerId);
    report_id_out->set_project_id(kProjectId);
    report_id_out->set_report_config_id(report_config.id());
    EXPECT_EQ(store::kOK,
              report_store_->StartNewReport(
                  first_day_index, last_day_index, false, export_name,
                  report_config.report_type(), {0}, report_id_out));
    if (should_complete_reports_) {
      EXPECT_EQ(store::kOK, report_store_->EndReport(*report_id_out, true, ""));
    }
    started_report_ids_.push_back(*report_id_out);
    first_day_indices_.push_back(first_day_index);
    last_day_indices_.push_back(last_day_index);
    export_names_.push_back(export_name);
    if (notifier_f_) {
      notifier_f_(started_report_ids_.size());
    }
    return grpc::Status::OK;
  }

  std::vector<ReportId> TakeStartedReportIds() {
    auto temp = std::move(started_report_ids_);
    started_report_ids_.clear();
    return temp;
  }

  std::vector<uint32_t> TakeFirstDayIndices() {
    auto temp = std::move(first_day_indices_);
    first_day_indices_.clear();
    return temp;
  }

  std::vector<uint32_t> TakeLastDayIndices() {
    auto temp = std::move(last_day_indices_);
    last_day_indices_.clear();
    return temp;
  }

  std::vector<std::string> TakeExportNames() {
    auto temp = std::move(export_names_);
    export_names_.clear();
    return temp;
  }

  void set_notifier_func(std::function<void(size_t)> f) { notifier_f_ = f; }
  void set_should_complete_reports(bool b) { should_complete_reports_ = b; }

 private:
  std::vector<ReportId> started_report_ids_;
  std::vector<uint32_t> first_day_indices_;
  std::vector<uint32_t> last_day_indices_;
  std::vector<std::string> export_names_;
  std::shared_ptr<ReportStore> report_store_;
  std::function<void(size_t)> notifier_f_;
  bool should_complete_reports_ = false;
};

class ReportSchedulerTest : public ::testing::Test {
 public:
  void SetUp() {
    auto report_parse_result =
        ReportRegistry::FromString(kReportConfigText, nullptr);
    EXPECT_EQ(config::kOK, report_parse_result.second);
    report_registry_.reset((report_parse_result.first.release()));
    data_store_.reset(new MemoryStore());
    data_store_->DeleteAllRows(DataStore::kReportMetadata);
    report_store_.reset(new ReportStore(data_store_));
    report_starter_.reset(new FakeReportStarter(report_store_));
    scheduler_.reset(new ReportScheduler(report_registry_, report_store_,
                                         report_starter_.get(),
                                         std::chrono::milliseconds(1)));
    clock_.reset(new IncrementingClock());
    clock_->set_time(util::FromUnixSeconds(kStartingTimeSeconds));
    clock_->set_increment(std::chrono::seconds(1));
    scheduler_->clock_ = clock_;
    report_store_->set_clock(clock_);
  }

  const ReportConfig* GetReportConfig() {
    return report_registry_->Get(kCustomerId, kProjectId, kReportConfigId);
  }

  // Performs the main logic for the ProcessOneReport test below.
  std::vector<ReportId> DoProcessOneReportTest(
      uint32_t current_day_index, std::vector<uint32_t> expected_day_indices) {
    ProcessOneReport(*GetReportConfig(), current_day_index);
    auto started_report_ids = report_starter_->TakeStartedReportIds();
    auto first_day_indices = report_starter_->TakeFirstDayIndices();
    auto last_day_indices = report_starter_->TakeLastDayIndices();
    auto export_names = report_starter_->TakeExportNames();
    size_t expected_num = expected_day_indices.size();
    EXPECT_EQ(expected_num, started_report_ids.size());
    EXPECT_EQ(expected_num, first_day_indices.size());
    EXPECT_EQ(expected_num, last_day_indices.size());
    EXPECT_EQ(expected_num, export_names.size());
    for (size_t i = 0; i < expected_num; i++) {
      uint32_t expected_day_index = expected_day_indices[i];
      std::ostringstream stream;
      stream << "report_1_1_42_" << expected_day_index << "_"
             << expected_day_index;
      std::string expected_export_name = stream.str();
      EXPECT_EQ(expected_day_index, first_day_indices[i]);
      EXPECT_EQ(expected_day_index, last_day_indices[i]);
      EXPECT_EQ(expected_export_name, export_names[i]);
    }
    return started_report_ids;
  }

  void SetSchedulerClock(std::shared_ptr<util::ClockInterface> clock) {
    scheduler_->clock_ = clock;
  }

  // Performs the logic for checking the results at the end of DoRunTest()
  // below.
  void CheckRunResults(uint32_t report_config_id, uint32_t finalization_days) {
    // Query for all instances of the given report config.
    auto response = report_store_->QueryReports(
        kCustomerId, kProjectId, report_config_id, 0, UINT64_MAX, 10000, "");
    EXPECT_EQ(store::kOK, response.status);

    // Accumulate the counts of the number of instances of the report config for
    // each day.
    std::map<uint32_t, size_t> day_counts;
    for (const auto& r : response.results) {
      day_counts[r.report_metadata.first_day_index()]++;
    }

    // During the makeup period, prior to the finalization cutoff for the
    // first day, there should be exactly one report per day. This is because
    // for days prior to the finalization cutoff we only run the report once.
    for (uint32_t day_index = kFirstDayIndex - FLAGS_daily_report_makeup_days;
         day_index <= kFirstDayIndex - finalization_days; day_index++) {
      EXPECT_EQ(1u, day_counts[day_index]) << "day_index=" << day_index;
    }

    // After the first day there should be exactly finalization_days * 6
    // reports per day. This is because for each day we run 6 reports for every
    // day that has not yet been finalized. An edge case is if
    // finalization_days == 0 in which case there should be one report per day.
    size_t expected_count =
        (finalization_days == 0 ? 1 : finalization_days * 6);
    for (uint32_t day_index = kFirstDayIndex + 1;
         day_index <= kFirstDayIndex + 10; day_index++) {
      EXPECT_EQ(expected_count, day_counts[day_index])
          << "report_config_id=" << report_config_id
          << " day_index=" << day_index;
    }

    // The number of reports run on the first day for the days that have not
    // yet been finalized is messy so during the pre-finalization period for
    // the first day we are only doing a sanity check: There should be more
    // than one and at most finalization_days*6 reports. Note that if
    // finalization_days=0 this is vacuous.
    for (uint32_t day_index = kFirstDayIndex - finalization_days + 1;
         day_index <= kFirstDayIndex; day_index++) {
      EXPECT_GT(day_counts[day_index], 1u);
      EXPECT_LE(day_counts[day_index], finalization_days * 6);
    }
  }

  // Tests the full operation of the scheduler thread. We invoke start() in
  // order to start the sdheduler thread. We arrange for the scheduler thread
  // to stop after 1000 iterations of the run loop. We then check the results
  // by inspecting the contents of the ReportStore.
  void DoRunTest() {
    // We give the ReportScheduler its own IncrementingClock with an increment
    // of 4 hours. This means that every 6 iterations through the Run()
    // loop will increment the current day index, so that each report
    // may be executed up to 6 times per day.
    std::shared_ptr<IncrementingClock> clock(new IncrementingClock());
    clock->set_time(util::FromUnixSeconds(kStartingTimeSeconds));
    clock->set_increment(std::chrono::seconds(60 * 60 * 4));
    SetSchedulerClock(clock);

    // We arrange for the ReportStarter to not only start reports but also
    // complete them successfully. If we did not do this then the
    // ReportScheduler would refuse to reschedule a report again on the same day
    // becuase it would think that the previous execution had not completed.
    report_starter_->set_should_complete_reports(true);

    // We arrange for the scheduler thread to notify this thread after
    // 1000 reports have been generated.
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    report_starter_->set_notifier_func(
        [&cv, &mu, &done](size_t num_reports_started) {
          if (num_reports_started > 1000) {
            std::lock_guard<std::mutex> lock(mu);
            done = true;
            cv.notify_all();
          }
        });

    // We start the scheduler thread.
    scheduler_->Start();

    // We wait for the scheduler thread to notify this thread that 1000 reports
    // have been generated.
    {
      std::unique_lock<std::mutex> lock(mu);
      cv.wait(lock, [&done] { return done; });
    }

    // We delete the ReportScheduler, which stops the scheduler thread.
    scheduler_.reset();

    // We check the restuls for our five report configs.
    CheckRunResults(kReportConfigId, kReportFinalizationDays);
    CheckRunResults(kReportConfigId2, kReportFinalizationDays2);
    CheckRunResults(kReportConfigId3, kReportFinalizationDays3);
    CheckRunResults(kReportConfigId4, kReportFinalizationDays4);
  }

 protected:
  std::shared_ptr<DataStore> data_store_;
  std::shared_ptr<ReportStore> report_store_;
  std::shared_ptr<ReportRegistry> report_registry_;
  std::unique_ptr<FakeReportStarter> report_starter_;
  std::unique_ptr<ReportScheduler> scheduler_;
  std::shared_ptr<IncrementingClock> clock_;

  void ProcessOneReport(const ReportConfig& report_config,
                        uint32_t current_day_index) {
    return scheduler_->ProcessOneReport(report_config, current_day_index);
  }
};

// Test the function ProcessOneReport. In this test we are not using the
// scheduler thread of the ReportScheduler--we never start it. Instead we
// directly invoke the private function ProcessOneReport() and check
// its results by interogating the FakeReportStarter.
TEST_F(ReportSchedulerTest, ProcessOneReport) {
  // The first time we run ProcessOneReport(), the ReportStore and the
  // ReportHistoryCache are empty. We should start one report for the current
  // day and one for each of the makeup days.
  uint32_t current_day_index = kFirstDayIndex;
  std::vector<uint32_t> expected_day_indices;
  for (uint32_t day_index = current_day_index - FLAGS_daily_report_makeup_days;
       day_index <= current_day_index; day_index++) {
    expected_day_indices.push_back(day_index);
  }
  std::vector<ReportId> started_report_ids;
  {
    SCOPED_TRACE("");
    started_report_ids =
        DoProcessOneReportTest(current_day_index, expected_day_indices);
  }

  // Now advance time by 10 minutes.
  int64_t current_time = kStartingTimeSeconds + kTenMinutes;
  clock_->set_time(util::FromUnixSeconds(current_time));

  // Its still the same day and none of the previously started reports have
  // completed, so this time ProcessOneReport() should not start any reports.
  expected_day_indices.clear();
  {
    SCOPED_TRACE("");
    DoProcessOneReportTest(current_day_index, expected_day_indices);
  }

  // Now complete all of the previously started reports. Suppose the first
  // one failed but all other ones succeeded.
  bool success = false;
  for (const auto& report_id : started_report_ids) {
    EXPECT_EQ(store::kOK, report_store_->EndReport(report_id, success, ""));
    success = true;
  }

  // Advance time by 10 minutes again.
  current_time += kTenMinutes;
  clock_->set_time(util::FromUnixSeconds(current_time));

  // This time ProcessOneReport() should only start a new report for the
  // days that have not yet been finalized, and one for the report that failed.
  expected_day_indices.clear();
  // This is for the day that failed.
  expected_day_indices.push_back(current_day_index -
                                 FLAGS_daily_report_makeup_days);
  // This is for the days that have not yet been finalized.
  for (uint32_t day_index = current_day_index - kReportFinalizationDays + 1;
       day_index <= current_day_index; day_index++) {
    expected_day_indices.push_back(day_index);
  }
  {
    SCOPED_TRACE("");
    started_report_ids =
        DoProcessOneReportTest(current_day_index, expected_day_indices);
  }

  // Now successfully complete all of the previously started reports.
  for (const auto& report_id : started_report_ids) {
    EXPECT_EQ(store::kOK, report_store_->EndReport(report_id, true, ""));
  }

  // Advance time by 10 minutes again.
  current_time += kTenMinutes;
  clock_->set_time(util::FromUnixSeconds(current_time));

  // This time ProcessOneReport() should only start a new report for the
  // days that have not yet been finalized.
  expected_day_indices.clear();
  for (uint32_t day_index = current_day_index - kReportFinalizationDays + 1;
       day_index <= current_day_index; day_index++) {
    expected_day_indices.push_back(day_index);
  }
  {
    SCOPED_TRACE("");
    started_report_ids =
        DoProcessOneReportTest(current_day_index, expected_day_indices);
  }

  // Now advance time by 24 hours.
  current_time += util::kNumUnixSecondsPerDay;
  clock_->set_time(util::FromUnixSeconds(current_time));
  current_day_index++;

  // None of the previously started reports from yesterday have completed.
  // This time ProcessOneReport() should only start a new report for the new
  // day.
  expected_day_indices.clear();
  expected_day_indices.push_back(current_day_index);
  {
    SCOPED_TRACE("");
    DoProcessOneReportTest(current_day_index, expected_day_indices);
  }

  // Now successfully complete all of the reports started yesterday.
  for (const auto& report_id : started_report_ids) {
    EXPECT_EQ(store::kOK, report_store_->EndReport(report_id, true, ""));
  }

  // Advance time by 10 minutes again.
  current_time += kTenMinutes;
  clock_->set_time(util::FromUnixSeconds(current_time));

  // This time ProcessOneReport() should only start a new report for the
  // days that have not yet been finalized, excluding the current day since
  // the report we started 10 minutes ago never finished.
  expected_day_indices.clear();
  for (uint32_t day_index = current_day_index - kReportFinalizationDays + 1;
       day_index < current_day_index; day_index++) {
    expected_day_indices.push_back(day_index);
  }
  {
    SCOPED_TRACE("");
    started_report_ids =
        DoProcessOneReportTest(current_day_index, expected_day_indices);
  }
}

// Tests the Run method using the default value of
// FLAGS_daily_report_makeup_days
TEST_F(ReportSchedulerTest, Run) { DoRunTest(); }

// Tests the Run method using
// FLAGS_daily_report_makeup_days = 2
TEST_F(ReportSchedulerTest, Run2) {
  gflags::FlagSaver s1;
  FLAGS_daily_report_makeup_days = 2;
  SetUp();
  DoRunTest();
}

// Tests the Run method using
// FLAGS_daily_report_makeup_days = 1
TEST_F(ReportSchedulerTest, Run1) {
  gflags::FlagSaver s1;
  FLAGS_daily_report_makeup_days = 1;
  SetUp();
  DoRunTest();
}

// Tests the Run method using
// FLAGS_daily_report_makeup_days = 0
TEST_F(ReportSchedulerTest, Run0) {
  gflags::FlagSaver s1;
  FLAGS_daily_report_makeup_days = 0;
  SetUp();
  DoRunTest();
}

}  // namespace analyzer
}  // namespace cobalt
