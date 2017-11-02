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

#ifndef COBALT_ANALYZER_STORE_OBSERVATION_STORE_INTERNAL_H_
#define COBALT_ANALYZER_STORE_OBSERVATION_STORE_INTERNAL_H_

// This file contains the declarations of private implementation functions
// that need to be accessible to unit tests. Non-test clients should not
// access these functions directly.

#include <string>

namespace cobalt {
namespace analyzer {
namespace store {
namespace internal {

// Returns the row key that encapsulates the given data.
std::string RowKey(uint32_t customer_id, uint32_t project_id,
                   uint32_t metric_id, uint32_t day_index, uint64_t random,
                   uint32_t hash);

// Returns a 32-bit hash of (|observation|, |metadata|) appropriate for use as
// the <hash> component of a row key.
uint32_t HashObservation(const Observation& observation,
                         const ObservationMetadata metadata);

// Returns the day_index encoded by |row_key|.
uint32_t DayIndexFromRowKey(const std::string& row_key);

// Returns the lexicographically least row key for rows with the given
// data.
std::string RangeStartKey(uint32_t customer_id, uint32_t project_id,
                          uint32_t metric_id, uint32_t day_index);

// Returns the lexicographically least row key that is greater than all row
// keys for rows with the given metadata, if day_index < UINT32_MAX. In the case
// that |day_index| = UINT32_MAX, returns the lexicographically least row key
// that is greater than all row keys for rows with the given values of
// the other parameters.
std::string RangeLimitKey(uint32_t customer_id, uint32_t project_id,
                          uint32_t metric_id, uint32_t day_index);

// Generates a new row key for a row for the given Observation.
std::string GenerateNewRowKey(const ObservationMetadata& metadata,
                              const Observation& observation);

bool ParseEncryptedObservationPart(ObservationPart* observation_part,
                                   std::string bytes);

bool ParseEncryptedSystemProfile(SystemProfile* system_profile,
                                 std::string bytes);

}  // namespace internal
}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_OBSERVATION_STORE_INTERNAL_H_
