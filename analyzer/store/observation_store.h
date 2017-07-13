// Copyright 2016 The Fuchsia Authors
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

#ifndef COBALT_ANALYZER_STORE_OBSERVATION_STORE_H_
#define COBALT_ANALYZER_STORE_OBSERVATION_STORE_H_

#include <memory>
#include <string>
#include <vector>

#include "./observation.pb.h"
#include "analyzer/store/data_store.h"

namespace cobalt {
namespace analyzer {
namespace store {

// An ObservationStore is used for storing and retrieving Observations.
// Observations are added to the store by the Analyzer Service when they
// are received from the Shuffler. Observations are queried from the
// store by ReportGenerator.
class ObservationStore {
 public:
  // Constructs an ObservationStore that wraps an underlying data store.
  explicit ObservationStore(std::shared_ptr<DataStore> store);

  // Adds an Observation and its metadata to the store.
  Status AddObservation(const ObservationMetadata& metadata,
                        const Observation& observation);

  // Adds a batch of Observations with a common set of metadata to the store.
  Status AddObservationBatch(const ObservationMetadata& metadata,
                             const std::vector<Observation>& observations);

  // A QueryResult represents one of the results contained in the QueryResponse
  // returned from QueryObservations(). This is a move-only type.
  struct QueryResult {
    // Default constructor
    QueryResult() {}

    // Move constructor.
    QueryResult(QueryResult&& other) : day_index(other.day_index) {
      observation.Swap(&other.observation);
    }

    // The day_index will be between the |start_day_index| and the
    // |end_day_index| passed to QueryObservations().
    uint32_t day_index;

    // The observation will only contain the parts requested in the
    // invocation of QueryObservations().
    Observation observation;
  };

  // A QueryResponse is returned from QueryObservations().
  struct QueryResponse {
    // status will be kOK on success or an error status on failure.
    // If there was an error then the other fields of QueryResponse
    // should be ignored.
    Status status;

    // If status is kOK then this is the list of results.
    std::vector<QueryResult> results;

    // If status is kOK and pagination_token is not empty, it indicates that
    // there were more results than could be returned in a single invocation
    // of QueryObservations(). Use this token as an input to another invocation
    // of QueryObservations() in order to obtain the next batch of results.
    // Note that it is possible for pagination_token to be non-empty even if the
    // number of results returned is fewer than the |max_results| specified in
    // the query.
    std::string pagination_token;
  };

  // Queries the observation store for a range of observations with the
  // given |customer_id|, |project_id|, |metric_id|.
  //
  // |start_day_index| and |end_day_index| specify an inclusive range of
  // day indices that the query is restricted to. If
  // start_day_index > end_day_index then the returned status will be
  // kInvalidArguments. It is permissible for start_day_index = 0 or
  // end_day_index = UINT32_MAX.
  //
  // If |parts| is not empty then the returned Observations will only contain
  // the specified parts. If |parts| is empty there will be no restriction
  // on observation parts.
  //
  // |max_results| must be positive and at most |max_results| will be returned.
  // The number of returned results may be less than |max_results| for
  // several reasons. The caller must look at whether or not the
  // |pagination_token| in the returned QueryResponse is empty in order to
  // determine if there are further results that may be queried.
  //
  // If |pagination_token| is not empty then it should be the pagination_token
  // from a QueryResponse that was returned from a previous invocation of
  // of this method with the same values for all of the other arguments.
  // This query will be restricted to start after the last result returned from
  // that previous query. A typical pattern is to invoke this method in a
  // loop passing the pagination_token returned from one invocation into
  // the following invocation. If pagination_token is not consistent with
  // the other arguments then the returned status will be kInvalidArguments.
  //
  // See the comments on |QueryResponse| for an explanation of how
  // to interpret the response.
  QueryResponse QueryObservations(uint32_t customer_id, uint32_t project_id,
                                  uint32_t metric_id, uint32_t start_day_index,
                                  uint32_t end_day_index,
                                  std::vector<std::string> parts,
                                  size_t max_results,
                                  std::string pagination_token);

  // Permanently deletes all observations in the observation store for the
  // given metric.
  Status DeleteAllForMetric(uint32_t customer_id, uint32_t project_id,
                            uint32_t metric_id);

 private:
  // The underlying data store.
  const std::shared_ptr<DataStore> store_;
};

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_OBSERVATION_STORE_H_
