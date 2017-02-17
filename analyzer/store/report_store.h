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

#ifndef COBALT_ANALYZER_STORE_REPORT_STORE_H_
#define COBALT_ANALYZER_STORE_REPORT_STORE_H_

#include <memory>
#include <string>
#include <vector>

#include "analyzer/report_master/report_internal.pb.h"
#include "analyzer/report_master/report_master.pb.h"
#include "analyzer/store/data_store.h"
#include "util/datetime_util.h"

namespace cobalt {
namespace analyzer {
namespace store {

// A ReportStore is used for storing and retrieving Cobalt reports. A
// report is the final output of the Cobalt pipeline--the result of the privacy-
// preserving analysis.
class ReportStore {
 public:
  // Constructs a ReportStore that wraps an underlying data store.
  explicit ReportStore(std::shared_ptr<DataStore> store);

  // Generates a new ReportId and writes information into the ReportStore to
  // indicate that the report with that ID is in the IN_PROGRESS state. This
  // method should be invoked prior to starting to add new rows to a report
  // via the AddReportRow() method.
  //
  // |first_day_index| and |last_day_index| specify the range of day indices
  // for which Observations will be analyzed for this report.
  //
  // |requested| indicates whether this report is being explicitly requested
  // as opposed to being generated by a regular schedule.
  //
  // |report_id| is used for both input and output. On input all fields other
  // then the |instance_id| and |creation_time_seconds| should be set. This
  // method will set those fields thereby forming a new unique ReportId.
  //
  // Note that in |report_id| the |variable_slice| field should be set by the
  // caller. For single-variable reports the variable_slice field should be left
  // at the default value of VARIABLE_1. For two-variable reports the
  // variable_slice field should be set to whichever variable slice is first
  // being generated. The other variable slices should be created and started
  // not with this method but rather with CreateSecondarySlice() and
  // StartSecondarySlice().
  Status StartNewReport(uint32_t first_day_index, uint32_t last_day_index,
                        bool requested, ReportId* report_id);

  // Writes information into the ReportStore to indicate that a report
  // corresponding to a secondary variable slice is in the WAITING_TO_START
  // state. This method is in support of two-variable reports. The method
  // StartNewReport() is used to start the report for the first variable slice
  // and this method is used to *create* but *not start* another report
  // for a secondary slice. This method may be invoked at any time
  // after StartNewReport() has been invoked for the first variable slice. This
  // is useful so that the secondary slices are registered in the ReportStore
  // before the actual generation of the first variable slice begins. The
  // method StartSecondarySlice() should later be invoked when it is time
  // to put the secondary slice into the IN_PROGRESS_STATE which must be done
  // before the generation of the secondary slice begins.
  //
  // slice: The variable slice that is being created, but not started.
  //
  // report_id: This is used for both input and output. On input this should
  // be a complete report_id that was earlier returned from StartNewReport()
  // or from this method. The |variable_slice| field of |report_id| will be
  // updated to be equal to |slice|, thereby forming a new ReportId which must
  // not yet exist in the ReportStore. The |first_day_index|, |last_day_index|,
  // and |requested| fields of ReportMetadataLite will be copied from the
  // existing report into the new report.
  //
  // Returns kOK on success, kNotFound if there is no existing report with
  // the ReportId passed in, and kAlreadyExists if there is already a
  // report with and ID of the new value of report_id obtained by setting the
  // |variable_slice| field to |slice|.
  Status CreateSecondarySlice(VariableSlice slice, ReportId* report_id);

  // Writes information into the ReportStore to indicate that the report
  // with the gvien |report_id| is in the IN_PROGRESS state. The report must
  // already exist in the ReportStore and it must be in the
  // WAITING_TO_START state.
  //
  // This method is in support of two-variable reports. The method
  // StartNewReport() is used to start the report for the first variable slice.
  // The method CreateSecondarySlice() is used to *create* but *not start*
  // a report for a secondary slice. Finally this method is used to start the
  // secondary slice. This method should be invoked before the actual
  // generation of the secondary variable slice begins.
  //
  // report_id: The ID of the report to be started. This should have been
  // returned from CreateSecondarySlice.
  //
  // Returns kOK on success, kNotFound if there is no existing report with
  // the ReportId passed in, and kPreconditionFailed if the report is not
  // in the WAITING_TO_START state.
  Status StartSecondarySlice(const ReportId& report_id);

  // Writes information into the ReportStore to indicate that the report with
  // the given |report_id| has ended. If |success| is true then the report
  // will now be in the COMPLETED_SUCCESSFULLY state, otherwise it will now be
  // in the TERMINATED state. The |message| may hold additional information
  // about the report  such as an error message in the case |successful| is
  // false. Returns kOK on success or kNotFound if there is no report with
  // the given report_id.
  Status EndReport(const ReportId& report_id, bool success,
                   std::string message);

  // Adds ReportRows to the ReportStore for the report with the given id.
  // This method should be invoked only after StartNewReport has been invoked
  // and the ReportId is therefore complete. This method is invoked
  // repeatedly in order to output the results of an analysis. After all of the
  // rows have been added with this method, the method EndReport() should be
  // invoked.
  //
  // Note that a ReportId indicates the VariableSlice that a report is for
  // and a ReportRow contains data for a VariableSlice. In other words
  // a ReportRow either contains a |value| in which case it is for VARIABLE_1
  // or a |value2| in which case it is for VARIABLE_2 or both a |value| and
  // and |value2| in which case it is for JOINT. This method will check that
  // the VariableSlice indicated by |report_id| and the VariableSlice
  // indicated by the ReportRows in |report_rows| are the same. Returns
  // kInvalidArguments if not.
  Status AddReportRows(const ReportId& report_id,
                       const std::vector<ReportRow>& report_rows);

  // Gets the ReportMetadataLite for the report with the specified id.
  Status GetMetadata(const ReportId& report_id,
                     ReportMetadataLite* metadata_out);

  // Gets the Report with the specified id.
  // TODO(rudominer) Consider not assuming a report fits in memory.
  Status GetReport(const ReportId& report_id, ReportMetadataLite* metadata_out,
                   ReportRows* report_out);

  // A ReportRecord is one of the results contained in the QueryReportsResponse
  // returned from QueryReports(). It contains only meta-data. The report data
  // is represented by ReportRows. This is a move-only type.
  struct ReportRecord {
    // Default constructor
    ReportRecord() {}

    // Move constructor.
    ReportRecord(ReportRecord&& other) {
      report_id.Swap(&other.report_id);
      report_metadata.Swap(&other.report_metadata);
    }

    ReportId report_id;
    ReportMetadataLite report_metadata;
  };

  // A QueryReportsResponse is returned from QueryReports().
  struct QueryReportsResponse {
    // status will be kOK on success or an error status on failure.
    // If there was an error then the other fields of QueryReportsResponse
    // should be ignored.
    Status status;

    // If status is kOK then this is the list of results of the query.
    std::vector<ReportRecord> results;

    // If status is kOK and pagination_token is not empty, it indicates that
    // there were more results than could be returned in a single invocation
    // of QueryReports(). Use this token as an input to another invocation
    // of QueryReports() in order to obtain the next batch of results.
    // Note that it is possible for pagination_token to be non-empty even if the
    // number of results returned is fewer than the |max_results| specified in
    // the query.
    std::string pagination_token;
  };

  // Queries the ReportStore for the list of reports that exist for the
  // given |customer_id|, |project_id|, |report_config_id|.
  //
  // |interval_start_time_seconds| and |interval_end_time_seconds| specify
  // a half-open interval of |creation_time_seconds| that the query is
  // restricted to. That is, the query will only return ReportRecords for which
  // the |creation_time_seconds| field of the |report_id| is in the range
  // [interval_start_time_seconds, interval_end_time_seconds).
  //
  // |max_results| must be positive and at most |max_results| will be returned.
  // The number of returned results may be less than |max_results| for
  // several reasons. The caller must look at whether or not the
  // |pagination_token| in the returned QueryReportsResponse is empty in order
  // to determine if there are further results that may be queried.
  //
  // If |pagination_token| is not empty then it should be the pagination_token
  // from a QueryReportsResponse that was returned from a previous invocation of
  // of this method with the same values for all of the other arguments.
  // This query will be restricted to start after the last result returned from
  // that previous query. A typical pattern is to invoke this method in a
  // loop passing the pagination_token returned from one invocation into
  // the following invocation. If pagination_token is not consistent with
  // the other arguments then the returned status will be kInvalidArguments.
  //
  // See the comments on |QueryReportsResponse| for an explanation of how
  // to interpret the response.
  QueryReportsResponse QueryReports(uint32_t customer_id, uint32_t project_id,
                                    uint32_t report_config_id,
                                    int64_t interval_start_time_seconds,
                                    int64_t interval_end_time_seconds,
                                    size_t max_results,
                                    std::string pagination_token);

  static std::string ToString(const ReportId& report_id);

  // Sets the clock used by the ReportStore for obtaining the current time.
  // Mostly useful for tests.
  void set_clock(std::shared_ptr<util::ClockInterface> clock) {
    clock_ = clock;
  }

 private:
  friend class ReportStorePrivateTest;
  friend class ReportStoreTestUtils;

  // Makes all instantiations of ReportStoreAbstractTest friends.
  template <class X>
  friend class ReportStoreAbstractTest;

  // Makes the row key for the report_metadata table that corresponds to the
  // given |report_id|.
  static std::string MakeMetadataRowKey(const ReportId& report_id);

  // Makes the first possible row key for the report_metadata table for the
  // given data.
  static std::string MetadataRangeStartKey(uint32_t customer_id,
                                           uint32_t project_id,
                                           uint32_t report_config_id,
                                           int64_t creation_time_seconds);

  // Makes the first possible row key for the report_rows table for
  // the given |report_id|.
  static std::string ReportStartRowKey(const ReportId& report_id);

  // Makes the last possible row key for the report_rows table for
  // the given |report_id|.
  static std::string ReportEndRowKey(const ReportId& report_id);

  // Generates a new row key for the report_rows table for the report with
  // the given report_id. Each time this method is invoked a new row key
  // is generated.
  static std::string GenerateReportRowKey(const ReportId& report_id);

  // Makes the DataStore::Row that represents the arguments.
  DataStore::Row MakeDataStoreRow(const ReportId& report_id,
                                  const ReportMetadataLite& metadata);

  // Write a row into the report_metadata table to represent the arguemnts.
  Status WriteMetadata(const ReportId& report_id,
                       const ReportMetadataLite& metadata);

  // Write many rows into the report_metadata table to represent the arguments.
  // CHECK fails if |report_ids| and |metadata| do not have the same length.
  Status WriteBulkMetadata(const std::vector<ReportId>& report_ids,
    const std::vector<ReportMetadataLite>& metadata);

  // The underlying data store.
  const std::shared_ptr<DataStore> store_;

  std::shared_ptr<util::ClockInterface> clock_;
};

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_REPORT_STORE_H_
