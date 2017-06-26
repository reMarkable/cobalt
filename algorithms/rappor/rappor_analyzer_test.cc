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
#include "algorithms/rappor/rappor_analyzer.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "algorithms/rappor/rappor_encoder.h"
#include "algorithms/rappor/rappor_test_utils.h"
#include "encoder/client_secret.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace rappor {

using encoder::ClientSecret;

namespace {

std::string CandidateString(int i) {
  return std::string("candidate string") + std::to_string(i);
}

// Populates |candidate_list| with |num_candidates| candidates;
void PopulateRapporCandidateList(uint32_t num_candidates,
                                 RapporCandidateList* candidate_list) {
  candidate_list->Clear();
  for (size_t i = 0; i < num_candidates; i++) {
    candidate_list->add_candidates(CandidateString(i));
  }
}

// Makes a RapporConfig with the given data (and num_hashes=2).
RapporConfig Config(uint32_t num_bloom_bits, uint32_t num_cohorts,
                    uint32_t num_hashes, double p, double q) {
  RapporConfig config;
  config.set_num_bloom_bits(num_bloom_bits);
  config.set_num_hashes(num_hashes);
  config.set_num_cohorts(num_cohorts);
  config.set_prob_0_becomes_1(p);
  config.set_prob_1_stays_1(q);
  return config;
}

}  // namespace

class RapporAnalyzerTest : public ::testing::Test {
 protected:
  // Sets the member variable analyzer_ to a new RapporAnalyzer configured
  // with the given arguments and the current values of prob_0_becomes_1_,
  // prob_1_stays_1_.
  void SetAnalyzer(uint32_t num_candidates, uint32_t num_bloom_bits,
                   uint32_t num_cohorts, uint32_t num_hashes) {
    PopulateRapporCandidateList(num_candidates, &candidate_list_);
    config_ = Config(num_bloom_bits, num_cohorts, num_hashes, prob_0_becomes_1_,
                     prob_1_stays_1_);
    analyzer_.reset(new RapporAnalyzer(config_, &candidate_list_));
  }

  void BuildCandidateMap() {
    EXPECT_EQ(grpc::OK, analyzer_->BuildCandidateMap().error_code());

    uint32_t num_candidates =
        analyzer_->candidate_map_.candidate_list->candidates_size();
    uint32_t num_cohorts = analyzer_->config_->num_cohorts();
    uint32_t num_hashes = analyzer_->config_->num_hashes();
    uint32_t num_bits = analyzer_->config_->num_bits();

    // Expect the number of candidates to be correct,
    EXPECT_EQ(num_candidates,
              analyzer_->candidate_map_.candidate_cohort_maps.size());

    // and for each candidate...
    for (size_t candidate = 0; candidate < num_candidates; candidate++) {
      // expect the number of cohorts to be correct,
      EXPECT_EQ(num_cohorts,
                analyzer_->candidate_map_.candidate_cohort_maps[candidate]
                    .cohort_hashes.size());

      // and for each cohort...
      for (size_t cohort = 0; cohort < num_cohorts; cohort++) {
        // expect the number of hashes to be correct,
        EXPECT_EQ(num_hashes,
                  analyzer_->candidate_map_.candidate_cohort_maps[candidate]
                      .cohort_hashes[cohort]
                      .bit_indices.size());

        // and for each hash...
        for (size_t hash = 0; hash < num_hashes; hash++) {
          // Expect the bit index to be in the range [0, num_bits).
          auto bit_index = GetCandidateMapValue(candidate, cohort, hash);
          EXPECT_GE(bit_index, 0u);
          EXPECT_LT(bit_index, num_bits);
        }
      }
    }
  }

  // This should be invoked after BuildCandidateMap. It returns the bit index
  // within the CandidateMap for the given |candidate_index|, |cohort_index|,
  // and |hash_index|.
  uint16_t GetCandidateMapValue(uint16_t candidate_index, uint16_t cohort_index,
                                uint16_t hash_index) {
    EXPECT_GT(analyzer_->candidate_map_.candidate_cohort_maps.size(),
              candidate_index);
    EXPECT_GT(analyzer_->candidate_map_.candidate_cohort_maps[candidate_index]
                  .cohort_hashes.size(),
              cohort_index);
    EXPECT_GT(analyzer_->candidate_map_.candidate_cohort_maps[candidate_index]
                  .cohort_hashes[cohort_index]
                  .bit_indices.size(),
              hash_index);
    return analyzer_->candidate_map_.candidate_cohort_maps[candidate_index]
        .cohort_hashes[cohort_index]
        .bit_indices[hash_index];
  }

  // Builds and returns a bit string (i.e. a string of ASCII '0's and '1's)
  // representing the Bloom filter implicitly stored within the CandidateMap
  // for the given |candidate_index| and |cohort_index|.
  std::string BuildBitString(uint16_t candidate_index, uint16_t cohort_index) {
    return BuildBinaryString(
        analyzer_->config_->num_bits(),
        analyzer_->candidate_map_.candidate_cohort_maps[candidate_index]
            .cohort_hashes[cohort_index]
            .bit_indices);
  }

  RapporConfig config_;
  std::unique_ptr<RapporAnalyzer> analyzer_;

  RapporCandidateList candidate_list_;

  // By default this test uses p=0, q=1. Individual tests may override this.
  double prob_0_becomes_1_ = 0.0;
  double prob_1_stays_1_ = 1.0;
};

// Tests the function BuildCandidateMap. We build one small CandidateMap and
// then we explicitly check every value against a known value. We have not
// independently verified the SHA-256 hash values and so rather than a test
// of correctness this is firstly a sanity test: we can eyeball the values
// and confirm they look sane, and secondly a regression test.
TEST_F(RapporAnalyzerTest, BuildCandidateMapSmallTest) {
  static const uint32_t kNumCandidates = 5;
  static const uint32_t kNumCohorts = 3;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 8;

  SetAnalyzer(kNumCandidates, kNumBloomBits, kNumCohorts, kNumHashes);
  BuildCandidateMap();

  // clang-format off
  int expected_bit_indices[kNumCandidates][kNumCohorts*kNumHashes] = {
  // cihj means cohort = i and hash-index = j.
  // c0h0 c0h1 c1h0 c1h1 c2h0 c2h2
      {3,   5,   2,   6,   3,   6},  // candidate 0
      {1,   5,   4,   7,   2,   0},  // candidate 1
      {3,   0,   2,   0,   1,   4},  // candidate 2
      {5,   1,   2,   4,   2,   4},  // candidate 3
      {1,   4,   3,   1,   2,   6},  // candidate 4
  };
  // clang-format on

  for (size_t candidate = 0; candidate < kNumCandidates; candidate++) {
    for (size_t cohort = 0; cohort < kNumCohorts; cohort++) {
      for (size_t hash = 0; hash < kNumHashes; hash++) {
        EXPECT_EQ(expected_bit_indices[candidate][cohort * kNumHashes + hash],
                  GetCandidateMapValue(candidate, cohort, hash))
            << "(" << candidate << "," << cohort * kNumHashes + hash << ")";
      }
    }
  }
}

// Tests the function BuildCandidateMap. We build many different CandidateMaps
// with many different parameters. We are testing firstly that the procedure
// completes without error, secondly that the shape of the produced
// data structure is correct and thirdly that the bit indexes are in the range
// [0, num_bloom_bits). The latter two checks occur inside of BuildCandidateMap.
TEST_F(RapporAnalyzerTest, BuildCandidateMapSmokeTest) {
  for (auto num_candidates : {11, 51, 99}) {
    for (auto num_cohorts : {23, 45}) {
      for (auto num_hashes : {2, 6, 7}) {
        for (auto num_bloom_bits : {16, 128}) {
          SetAnalyzer(num_candidates, num_bloom_bits, num_cohorts, num_hashes);
          BuildCandidateMap();
        }
      }
    }
  }
}

// Tests the function BuildCandidateMap. We test that the map that is built
// is consistent with the Bloom filters that are built by an encoder.
TEST_F(RapporAnalyzerTest, BuildCandidateMapCompareWithEncoder) {
  static const uint32_t kNumCandidates = 10;
  static const uint32_t kNumCohorts = 20;
  static const uint32_t kNumHashes = 5;
  static const uint32_t kNumBloomBits = 64;

  SetAnalyzer(kNumCandidates, kNumBloomBits, kNumCohorts, kNumHashes);
  BuildCandidateMap();

  for (size_t candidate = 0; candidate < kNumCandidates; candidate++) {
    // Construct a new encoder with a new ClientSecret so that a random
    // cohort is selected.
    RapporEncoder encoder(config_, ClientSecret::GenerateNewSecret());

    // Encode the current candidate string using |encoder|.
    ValuePart value_part;
    value_part.set_string_value(CandidateString(candidate));
    RapporObservation observation;
    encoder.Encode(value_part, &observation);

    // Since p=0 and q=1 the RapporObservation contains the raw Bloom filter
    // with no noise added. Confirm that the BloomFilter is the same as
    // the one implied by the CandidateMap at the appropriate candidate
    // and cohort.
    EXPECT_EQ(BuildBitString(candidate, encoder.cohort()),
              DataToBinaryString(observation.data()));
  }
}

}  // namespace rappor
}  // namespace cobalt
