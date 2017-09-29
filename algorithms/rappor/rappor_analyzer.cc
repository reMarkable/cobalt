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

  // TODO(rudominer) We should cache candidate_matrix_ rather than recomputing
  // candidate_map_ and candidate_matrix_ each time.

  const uint32_t num_bits = config_->num_bits();
  const uint32_t num_cohorts = config_->num_cohorts();
  const uint32_t num_hashes = config_->num_hashes();
  const uint32_t num_candidates =
      candidate_map_.candidate_list->candidates_size();

  candidate_matrix_.resize(num_cohorts * num_bits, num_candidates);
  std::vector<Eigen::Triplet<float>> sparse_matrix_triplets;
  sparse_matrix_triplets.reserve(num_candidates * num_cohorts * num_hashes);

  int column = 0;
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
    int row_block_base = 0;
    for (size_t cohort = 0; cohort < num_cohorts; cohort++) {
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

      // bloom_filter is indexed "from the left". That is bloom_filter[0]
      // corresponds to the most significant bit of the first byte of the
      // Bloom filter.
      std::vector<bool> bloom_filter(num_bits, false);

      // Extract one bit index for each of the hashes in the Bloom filter.
      for (size_t hash_index = 0; hash_index < num_hashes; hash_index++) {
        uint32_t bit_index =
            RapporEncoder::ExtractBitIndex(hashed_value, hash_index, num_bits);
        hashes.bit_indices.push_back(bit_index);
        // |bit_index| is an index "from the right".
        bloom_filter[num_bits - 1 - bit_index] = true;
      }

      // Add triplets to the sparse matrix representation. For the current
      // column and the current block of rows we add a 1 into the row
      // corresponding to the index of each set bit in the Bloom filter.
      for (size_t bit_index = 0; bit_index < num_bits; bit_index++) {
        if (bloom_filter[bit_index]) {
          int row = row_block_base + bit_index;
          sparse_matrix_triplets.emplace_back(row, column, 1.0);
        }
      }

      // In our sparse matrix representation each cohort corresponds to a block
      // of |num_bits| rows.
      row_block_base += num_bits;
    }
    // In our sparse matrix representation a column corresponds to a candidate.
    column++;
    row_block_base = 0;
  }

  candidate_matrix_.setFromTriplets(sparse_matrix_triplets.begin(),
                                    sparse_matrix_triplets.end());

  return grpc::Status::OK;
}

}  // namespace rappor
}  // namespace cobalt
