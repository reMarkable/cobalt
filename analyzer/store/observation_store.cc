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

#include "analyzer/store/observation_store.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./observation.pb.h"
#include "analyzer/store/data_store.h"
#include "analyzer/store/observation_store_internal.h"
#include "glog/logging.h"
#include "util/crypto_util/random.h"

namespace cobalt {
namespace analyzer {
namespace store {

using internal::DayIndexFromRowKey;
using internal::GenerateNewRowKey;
using internal::ParseEncryptedObservationPart;
using internal::RangeLimitKey;
using internal::RangeStartKey;

// The internal namespace contains private implementation functions that need
// to be accessible to unit tests. The functions are declared in
// observation_store_internal.h.
namespace internal {

// Returns the row key that encapsulates the given data.
std::string RowKey(uint32_t customer_id, uint32_t project_id,
                   uint32_t metric_id, uint32_t day_index,
                   uint64_t current_time_millis, uint32_t random) {
  // We write five ten-digit numbers, plus one twenty-digit number plus five
  // colons. The string has size 76 to accommodate a trailing null character.
  std::string out(76, 0);

  // TODO(rudominer): Replace human-readable row key with smaller more efficient
  // representation.
  // TODO(rudominer): Use (random, time) instead of (time, random) because this
  // allows the ReportGenerator to be sharded based on random.
  std::snprintf(&out[0], out.size(), "%.10u:%.10u:%.10u:%.10u:%.20lu:%.10u",
                customer_id, project_id, metric_id, day_index,
                current_time_millis, random);

  // Discard the trailing null character.
  out.resize(75);

  return out;
}

// Returns the common prefix of all rows keys for the given metric.
std::string RowKeyPrefix(uint32_t customer_id, uint32_t project_id,
                         uint32_t metric_id) {
  // TODO(rudominer) This length corresponds to our current, temporary,
  // human-readable row-keys built in RowKey() above. This function needs
  // to change when the implementation changes. The prefix we return
  // includes three ten-digit numbers plus three colons.
  static const size_t kPrefixLength = 33;
  std::string row_key = RowKey(customer_id, project_id, metric_id, 0, 0, 0);
  row_key.resize(kPrefixLength);
  return row_key;
}

// Returns the day_index encoded by |row_key|.
uint32_t DayIndexFromRowKey(const std::string& row_key) {
  uint32_t day_index = 0;
  // Parse the string produced by the RowKey() function above. We skip three
  // ten-digit integers and three colons and then parse 10 digits.
  CHECK_GT(row_key.size(), 33u);
  std::sscanf(&row_key[33], "%10u", &day_index);
  return day_index;
}

// Returns the lexicographically least row key for rows with the given
// data.
std::string RangeStartKey(uint32_t customer_id, uint32_t project_id,
                          uint32_t metric_id, uint32_t day_index) {
  return RowKey(customer_id, project_id, metric_id, day_index, 0, 0);
}

// Returns the lexicographically least row key that is greater than all row
// keys for rows with the given metadata, if day_index < UINT32_MAX. In the case
// that |day_index| = UINT32_MAX, returns the lexicographically least row key
// that is greater than all row keys for rows with the given values of
// the other parameters.
std::string RangeLimitKey(uint32_t customer_id, uint32_t project_id,
                          uint32_t metric_id, uint32_t day_index) {
  if (day_index < UINT32_MAX) {
    return RowKey(customer_id, project_id, metric_id, day_index + 1, 0, 0);
  } else {
    // UINT32_MAX is already greater than all valid values of day_index.
    return RowKey(customer_id, project_id, metric_id, UINT32_MAX, 0, 0);
  }
}

// Returns the current time expressed as a number of milliseonds since the
// Unix epoch.
uint64_t CurrentTimeMillis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// Generates a new row key for a row with the given data.
std::string GenerateNewRowKey(uint32_t customer_id, uint32_t project_id,
                              uint32_t metric_id, uint32_t day_index) {
  cobalt::crypto::Random rand;
  int32_t random = rand.RandomUint32();
  return RowKey(customer_id, project_id, metric_id, day_index,
                CurrentTimeMillis(), random);
}

bool ParseEncryptedObservationPart(ObservationPart* observation_part,
                                   std::string bytes) {
  // TODO(rudominer) Arrange for ObservationParts to be encrypted.
  return observation_part->ParseFromString(bytes);
}

}  // namespace internal

ObservationStore::ObservationStore(std::shared_ptr<DataStore> store)
    : store_(store) {}

Status ObservationStore::AddObservation(const ObservationMetadata& metadata,
                                        const Observation& observation) {
  DataStore::Row row;
  row.key = GenerateNewRowKey(metadata.customer_id(), metadata.project_id(),
                              metadata.metric_id(), metadata.day_index());
  for (const auto& pair : observation.parts()) {
    std::string serialized_observation_part;
    pair.second.SerializeToString(&serialized_observation_part);
    // TODO(rudominer) Consider ways to avoid having so many copies of the
    // part names.
    row.column_values[pair.first] = std::move(serialized_observation_part);
  }
  return store_->WriteRow(DataStore::kObservations, std::move(row));
}

Status ObservationStore::AddObservationBatch(
    const ObservationMetadata& metadata,
    const std::vector<Observation>& observations) {
  std::vector<DataStore::Row> rows;
  for (const Observation& observation : observations) {
    DataStore::Row row;
    row.key = GenerateNewRowKey(metadata.customer_id(), metadata.project_id(),
                                metadata.metric_id(), metadata.day_index());
    for (const auto& pair : observation.parts()) {
      std::string serialized_observation_part;
      pair.second.SerializeToString(&serialized_observation_part);
      // TODO(rudominer) Consider ways to avoid having so many copies of the
      // part names.
      row.column_values[pair.first] = std::move(serialized_observation_part);
    }

    rows.emplace_back(std::move(row));
  }

  return store_->WriteRows(DataStore::kObservations, std::move(rows));
}

ObservationStore::QueryResponse ObservationStore::QueryObservations(
    uint32_t customer_id, uint32_t project_id, uint32_t metric_id,
    uint32_t start_day_index, uint32_t end_day_index,
    std::vector<std::string> parts, size_t max_results,
    std::string pagination_token) {
  ObservationStore::QueryResponse query_response;
  std::string start_row;
  bool inclusive = true;
  std::string range_start_key =
      RangeStartKey(customer_id, project_id, metric_id, start_day_index);
  if (!pagination_token.empty()) {
    // The pagination token should be the row key of the last row returned the
    // previous time this method was invoked.
    if (pagination_token < range_start_key) {
      query_response.status = kInvalidArguments;
      return query_response;
    }
    start_row.swap(pagination_token);
    inclusive = false;
  } else {
    start_row.swap(range_start_key);
  }

  std::string limit_row =
      RangeLimitKey(customer_id, project_id, metric_id, end_day_index);

  if (limit_row <= start_row) {
    query_response.status = kInvalidArguments;
    return query_response;
  }

  DataStore::ReadResponse read_response = store_->ReadRows(
      DataStore::kObservations, std::move(start_row), inclusive,
      std::move(limit_row), std::move(parts), max_results);

  query_response.status = read_response.status;
  if (query_response.status != kOK) {
    return query_response;
  }

  for (const DataStore::Row& row : read_response.rows) {
    // For each row of the read_response we add a query_result to the
    // query_response.
    query_response.results.emplace_back();
    auto& query_result = query_response.results.back();
    query_result.day_index = DayIndexFromRowKey(row.key);

    for (auto& pair : row.column_values) {
      const std::string& column_name = pair.first;
      const std::string& column_value = pair.second;
      // For each column_value in the row we add an ObservationPart.
      // The column_name is the part name and so the key to the map. The
      // The insert_result is a pair of the form < <key, value>, bool> where
      // the bool indicates whether or not the key was newly added to the map.
      auto insert_result = query_result.observation.mutable_parts()->insert(
          google::protobuf::Map<std::string, ObservationPart>::value_type(
              column_name, ObservationPart()));
      // The column names should all be unique so each insert should return
      // true.
      DCHECK(insert_result.second);
      // The ObservationPart is the value and so the second element of the
      // first element of insert_result.
      auto& observation_part = insert_result.first->second;
      // We deserialize the ObservationPart from the column value.
      if (!ParseEncryptedObservationPart(&observation_part, column_value)) {
        query_response.status = kOperationFailed;
        return query_response;
      }
    }
  }
  if (read_response.more_available) {
    // If the underling store says that there are more rows available, then
    // we return the row key of the last row as the pagination_token.
    if (read_response.rows.empty()) {
      // There Read operation indicated that there were more rows available yet
      // it did not return even one row. In this pathological case we return
      // an error.
      query_response.status = kOperationFailed;
      return query_response;
    }
    size_t last_index = read_response.rows.size() - 1;
    query_response.pagination_token.swap(read_response.rows[last_index].key);
  }

  return query_response;
}

Status ObservationStore::DeleteAllForMetric(uint32_t customer_id,
                                            uint32_t project_id,
                                            uint32_t metric_id) {
  return store_->DeleteRowsWithPrefix(
      DataStore::kObservations,
      internal::RowKeyPrefix(customer_id, project_id, metric_id));
}

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
