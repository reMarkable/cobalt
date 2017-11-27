// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_history_cache.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/store/memory_store.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {

using store::DataStore;
using store::MemoryStore;
using store::ReportStore;

namespace {
uint32_t kDayIndexLowerBound = 12345;
uint32_t kCustomerId = 1;
uint32_t kProjectId = 1;

ReportConfig MakeReportConfig(uint32_t id) {
  ReportConfig report_config;
  report_config.set_customer_id(kCustomerId);
  report_config.set_project_id(kProjectId);
  report_config.set_id(id);
  return report_config;
}

ReportId MakeReportId(uint32_t report_config_id) {
  ReportId report_id;
  report_id.set_customer_id(kCustomerId);
  report_id.set_project_id(kProjectId);
  report_id.set_report_config_id(report_config_id);
  return report_id;
}

}  // namespace

class ReportHistoryCacheTest : public ::testing::Test {
 public:
  void SetUp() {
    data_store_.reset(new MemoryStore());
    data_store_->DeleteAllRows(DataStore::kReportMetadata);
    report_store_.reset(new ReportStore(data_store_));
    cache_.reset(new ReportHistoryCache(kDayIndexLowerBound, report_store_));
  }

  // Tests the method ReportHistoryCache::InProgress() in various scenarios:
  // At first the ReportStore is empty. Then a report for report_config_1 is
  // started for the given |day_index|. Then the report is completed either
  // successfully or not depending on the value of |success|. In all cases
  // we invoke
  // report_store_->InProgress(report_config_1_, day_index, day_index).
  void DoInProgressTest(uint32_t day_index, bool success) {
    // When no reports have been started then no reports are in progress.
    EXPECT_FALSE(cache_->InProgress(report_config_1_, day_index, day_index));

    // Start a report for report_config 1.
    ReportId report_id = MakeReportId(1);
    report_store_->StartNewReport(day_index, day_index, false, "", HISTOGRAM,
                                  std::vector<uint32_t>{}, &report_id);

    // The cache does not know that any reports are in progress.
    EXPECT_FALSE(cache_->InProgress(report_config_1_, day_index, day_index));

    // Tell the cache that a report has started.
    cache_->RecordStart(report_config_1_, day_index, day_index, report_id);

    // Now the cache knows that a report is in progress.
    EXPECT_TRUE(cache_->InProgress(report_config_1_, day_index, day_index));

    // End the report either successfully or not.
    report_store_->EndReport(report_id, success, "");

    // The cache will query to learn that the report is no longer in progress.
    EXPECT_FALSE(cache_->InProgress(report_config_1_, day_index, day_index));
  }

  // Tests the method ReportHistoryCache::CompletedSuccessfullyOrInProgress()
  // in various scenarios: First the initial sate of the ReportStore is
  // used. |expected_initial_val| specifes the value we expect
  // CompletedSuccessfullyOrInProgress() to return initially.
  // Then a non-one-off report is started for report_config_1.
  // Then the report is completed either successfully or not depending on the
  // value of |success|. In all cases we invoke
  //  cache_->InProgress(report_config_1_, day_index, day_index).
  void DoCompletedSuccessfullyOrInProgressTest(bool expected_initial_val,
                                               uint32_t day_index,
                                               bool success) {
    // Check the expected initial value.
    EXPECT_EQ(expected_initial_val,
              cache_->CompletedSuccessfullyOrInProgress(report_config_1_,
                                                        day_index, day_index));

    // Start a non-one-off report for report_config 1.
    ReportId report_id = MakeReportId(1);
    report_store_->StartNewReport(day_index, day_index, false, "", HISTOGRAM,
                                  std::vector<uint32_t>{}, &report_id);

    // The cache does not know that a report is in progress. But if it is
    // known that a report completed successfully that doesn't matter.
    EXPECT_EQ(expected_initial_val,
              cache_->CompletedSuccessfullyOrInProgress(report_config_1_,
                                                        day_index, day_index));

    // Tell the cache that a reort has started.
    cache_->RecordStart(report_config_1_, day_index, day_index, report_id);

    // Now the cache knows that a report is in progress.
    EXPECT_TRUE(cache_->CompletedSuccessfullyOrInProgress(
        report_config_1_, day_index, day_index));

    // End the report either successfully or not.
    report_store_->EndReport(report_id, success, "");

    // The cache will query to learn that the report is no longer in progress
    // and terminated either successfully or not. But if it is known that a
    // report completed successfully that doesn't matter.
    EXPECT_EQ(expected_initial_val || success,
              cache_->CompletedSuccessfullyOrInProgress(report_config_1_,
                                                        day_index, day_index));
  }

  // Invokes DoCompletedSuccessfullyOrInProgressTest() using an empty intial
  // ReportStore. The expected_initial_val in this case is false.
  void DoEmptyStoreCompletedSuccessfullyOrInProgressTest(uint32_t day_index,
                                                         bool success) {
    // When no reports have been started then the initial value of
    // CompletedSuccessfullyOrInProgress() should be false.
    DoCompletedSuccessfullyOrInProgressTest(false, day_index, success);
  }

  // Invokes DoCompletedSuccessfullyOrInProgressTest() using an initial
  // ReportStore in which a report for report_config_1 has been started
  // and completed. |one_off| determines whether or not this initial report
  // is a one-off report. |initial_success| determines whether or not this
  // initial report completed successfully.
  void DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      bool one_off, bool initial_success, uint32_t day_index, bool success) {
    // Insert a record into the ReportStore for a completed report for
    // report_config_1.
    ReportId report_id = MakeReportId(1);
    report_store_->StartNewReport(day_index, day_index, one_off, "", HISTOGRAM,
                                  std::vector<uint32_t>{}, &report_id);
    report_store_->EndReport(report_id, initial_success, "");

    // Also insert records for report_configs 2 and 3. These should have
    // no affect. Report 2 will still be in progress and report 3 will be
    // completed.
    ReportId report_id2 = MakeReportId(2);
    report_store_->StartNewReport(day_index, day_index, one_off, "", HISTOGRAM,
                                  std::vector<uint32_t>{}, &report_id2);
    cache_->RecordStart(report_config_2_, day_index, day_index, report_id2);
    ReportId report_id3 = MakeReportId(3);
    report_store_->StartNewReport(day_index, day_index, false, "", HISTOGRAM,
                                  std::vector<uint32_t>{}, &report_id3);
    cache_->RecordStart(report_config_3_, day_index, day_index, report_id3);
    report_store_->EndReport(report_id3, initial_success, "");

    // The first invocation of CompletedSuccessfullyOrInProgress() for
    // report_config_1 should return true just in case the previously completed
    // report was not a one-off report and was completed successfully.
    bool expected_initial_val = !one_off && initial_success;
    DoCompletedSuccessfullyOrInProgressTest(expected_initial_val, day_index,
                                            success);
  }

 protected:
  std::shared_ptr<DataStore> data_store_;
  std::shared_ptr<ReportStore> report_store_;
  std::unique_ptr<ReportHistoryCache> cache_;
  ReportConfig report_config_1_ = MakeReportConfig(1);
  ReportConfig report_config_2_ = MakeReportConfig(2);
  ReportConfig report_config_3_ = MakeReportConfig(3);
};

TEST_F(ReportHistoryCacheTest, InProgress1) {
  bool complete_successfully = true;
  DoInProgressTest(kDayIndexLowerBound, complete_successfully);
}

TEST_F(ReportHistoryCacheTest, InProgress2) {
  bool complete_successfully = false;
  DoInProgressTest(kDayIndexLowerBound, complete_successfully);
}

TEST_F(ReportHistoryCacheTest, InProgress3) {
  bool complete_successfully = true;
  DoInProgressTest(kDayIndexLowerBound + 1, complete_successfully);
}

TEST_F(ReportHistoryCacheTest, InProgress4) {
  bool complete_successfully = false;
  DoInProgressTest(kDayIndexLowerBound + 1, complete_successfully);
}

TEST_F(ReportHistoryCacheTest, InProgress5) {
  bool complete_successfully = true;
  DoInProgressTest(kDayIndexLowerBound + 30, complete_successfully);
}

TEST_F(ReportHistoryCacheTest, InProgress6) {
  bool complete_successfully = false;
  DoInProgressTest(kDayIndexLowerBound + 30, complete_successfully);
}

TEST_F(ReportHistoryCacheTest, EmptyStoreCompletedSuccessfullyOrInProgress1) {
  bool complete_successfully = true;
  DoEmptyStoreCompletedSuccessfullyOrInProgressTest(kDayIndexLowerBound,
                                                    complete_successfully);
}

TEST_F(ReportHistoryCacheTest, EmptyStoreCompletedSuccessfullyOrInProgress2) {
  bool complete_successfully = false;
  DoEmptyStoreCompletedSuccessfullyOrInProgressTest(kDayIndexLowerBound,
                                                    complete_successfully);
}

TEST_F(ReportHistoryCacheTest, EmptyStoreCompletedSuccessfullyOrInProgress3) {
  bool complete_successfully = true;
  DoEmptyStoreCompletedSuccessfullyOrInProgressTest(kDayIndexLowerBound + 1,
                                                    complete_successfully);
}

TEST_F(ReportHistoryCacheTest, EmptyStoreCompletedSuccessfullyOrInProgress4) {
  bool complete_successfully = false;
  DoEmptyStoreCompletedSuccessfullyOrInProgressTest(kDayIndexLowerBound + 1,
                                                    complete_successfully);
}

TEST_F(ReportHistoryCacheTest, EmptyStoreCompletedSuccessfullyOrInProgress5) {
  bool complete_successfully = true;
  DoEmptyStoreCompletedSuccessfullyOrInProgressTest(kDayIndexLowerBound + 30,
                                                    complete_successfully);
}

TEST_F(ReportHistoryCacheTest, EmptyStoreCompletedSuccessfullyOrInProgress6) {
  bool complete_successfully = false;
  DoEmptyStoreCompletedSuccessfullyOrInProgressTest(kDayIndexLowerBound + 30,
                                                    complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress1) {
  bool one_off = true;
  bool initial_success = true;
  bool complete_successfully = true;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound, complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress2) {
  bool one_off = false;
  bool initial_success = true;
  bool complete_successfully = true;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound, complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress3) {
  bool one_off = false;
  bool initial_success = false;
  bool complete_successfully = true;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound, complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress4) {
  bool one_off = false;
  bool initial_success = true;
  bool complete_successfully = false;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound, complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress5) {
  bool one_off = false;
  bool initial_success = false;
  bool complete_successfully = false;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound, complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress6) {
  bool one_off = true;
  bool initial_success = true;
  bool complete_successfully = true;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound + 1, complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress7) {
  bool one_off = false;
  bool initial_success = true;
  bool complete_successfully = true;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound + 1, complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress8) {
  bool one_off = false;
  bool initial_success = false;
  bool complete_successfully = true;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound + 1, complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress9) {
  bool one_off = false;
  bool initial_success = true;
  bool complete_successfully = false;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound + 1, complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress10) {
  bool one_off = false;
  bool initial_success = false;
  bool complete_successfully = false;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound + 1, complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress11) {
  bool one_off = true;
  bool initial_success = true;
  bool complete_successfully = true;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound + 30,
      complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress12) {
  bool one_off = false;
  bool initial_success = true;
  bool complete_successfully = true;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound + 30,
      complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress13) {
  bool one_off = false;
  bool initial_success = false;
  bool complete_successfully = true;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound + 30,
      complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress14) {
  bool one_off = false;
  bool initial_success = true;
  bool complete_successfully = false;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound + 30,
      complete_successfully);
}

TEST_F(ReportHistoryCacheTest,
       PreviousCompletionCompletedSuccessfullyOrInProgress15) {
  bool one_off = false;
  bool initial_success = false;
  bool complete_successfully = false;
  DoPreviousCompletionCompletedSuccessfullyOrInProgressTest(
      one_off, initial_success, kDayIndexLowerBound + 30,
      complete_successfully);
}

}  // namespace analyzer
}  // namespace cobalt
