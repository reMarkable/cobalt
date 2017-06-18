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

#include "algorithms/rappor/rappor_analyzer.h"

#include <glog/logging.h>

#include "algorithms/rappor/rappor_encoder.h"
#include "util/crypto_util/hash.h"

namespace cobalt {
namespace rappor {

using crypto::byte;

RapporAnalyzer::RapporAnalyzer(const RapporConfig& config,
                               const RapporCandidateList* candidates)
    : bit_counter_(config), config_(bit_counter_.config()) {
  candidate_map_.candidate_list = candidates;
  // candidate_map_.candidate_cohort_maps remains empty for now. It
  // will be populated by BuildCandidateMap.
}

bool RapporAnalyzer::AddObservation(const RapporObservation& obs) {
  return bit_counter_.AddObservation(obs);
}

grpc::Status RapporAnalyzer::Analyze(
    std::vector<CandidateResult>* results_out) {
  CHECK(results_out);
  auto status = BuildCandidateMap();
  if (!status.ok()) {
    return status;
  }

  //////////////////////////////////////////////////////
  // TODO(azani, mironov) Put LASSO analysis here.
  /////////////////////////////////////////////////////

  return grpc::Status::OK;
}

grpc::Status RapporAnalyzer::BuildCandidateMap() {
  if (!config_->valid()) {
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Invalid RapporConfig passed to constructor.");
  }

  // TODO(rudominer) Consider caching the CandidateMaps. The same RAPPOR
  // analysis will likely be run every day with the same RapporConfig and
  // RapporCandidateList but different sets of Observatons. Since the
  // CandidateMap does not depend on the Observations, we can cache them
  // (for example in Bigtable.) Alternatively, instead of caching the
  // CandidateMap itself we could also cache the sparse binary matrix that will
  // be generated based on the CandidateMap and used as one of the inputs
  // to the LASSO algorithm.

  uint32_t num_bits = config_->num_bits();
  uint32_t num_cohorts = config_->num_cohorts();
  uint32_t num_hashes = config_->num_hashes();

  for (const std::string& candidate :
       candidate_map_.candidate_list->candidates()) {
    // In rappor_encoder.cc it is not std::strings that are encoded but rather
    // |ValuePart|s. So here we want to take the candidate as a string and
    // convert it into a serialized |ValuePart|.
    ValuePart candidate_as_value_part;
    candidate_as_value_part.set_string_value(candidate);
    std::string serialized_candidate;
    candidate_as_value_part.SerializeToString(&serialized_candidate);

    // Append a CohortMap for this candidate.
    candidate_map_.candidate_cohort_maps.emplace_back();
    CohortMap& cohort_map = candidate_map_.candidate_cohort_maps.back();

    // Iterate through the cohorts.
    for (auto cohort = 0; cohort < num_cohorts; cohort++) {
      // Append an instance of |Hashes| for this cohort.
      cohort_map.cohort_hashes.emplace_back();
      Hashes& hashes = cohort_map.cohort_hashes.back();

      // Form one big hashed value of the serialized_candidate. This will be
      // used to obtain multiple bit indices.
      byte hashed_value[crypto::hash::DIGEST_SIZE];
      if (!RapporEncoder::HashValueAndCohort(serialized_candidate, cohort,
                                             num_hashes, hashed_value)) {
        return grpc::Status(grpc::INTERNAL,
                            "Hash operation failed unexpectedly.");
      }

      // Extract one bit index for each of the hashes in the Bloom filter.
      for (size_t hash_index = 0; hash_index < num_hashes; hash_index++) {
        hashes.bit_indices.push_back(
            RapporEncoder::ExtractBitIndex(hashed_value, hash_index, num_bits));
      }
    }
  }
  return grpc::Status::OK;
}

}  // namespace rappor
}  // namespace cobalt
