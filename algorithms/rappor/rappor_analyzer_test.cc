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

// Makes a RapporConfig with the given data.
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

// Given a string of "0"s and "1"s of length a multiple of 8, and a cohort,
// returns a RapporObservation for the given cohort whose data is equal to the
// bytes whose binary representation is given by the string.
RapporObservation RapporObservationFromString(
    uint32_t cohort, const std::string& binary_string) {
  RapporObservation obs;
  obs.set_cohort(cohort);
  obs.set_data(BinaryStringToData(binary_string));
  return obs;
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

    const uint32_t num_candidates =
        analyzer_->candidate_map_.candidate_list->candidates_size();
    const uint32_t num_cohorts = analyzer_->config_->num_cohorts();
    const uint32_t num_hashes = analyzer_->config_->num_hashes();
    const uint32_t num_bits = analyzer_->config_->num_bits();

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

    // Validate the associated sparse matrix.
    EXPECT_EQ(num_candidates, candidate_matrix().cols());
    EXPECT_EQ(num_cohorts * num_bits, candidate_matrix().rows());
    EXPECT_LE(num_candidates * num_cohorts, candidate_matrix().nonZeros());
    EXPECT_GE(num_candidates * num_cohorts * num_hashes,
              candidate_matrix().nonZeros());
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

  const Eigen::SparseMatrix<float, Eigen::RowMajor>& candidate_matrix() {
    return analyzer_->candidate_matrix_;
  }

  void AddObservation(uint32_t cohort, std::string binary_string) {
    EXPECT_TRUE(analyzer_->AddObservation(
        RapporObservationFromString(cohort, binary_string)));
  }

  void ExtractEstimatedBitCountRatios(Eigen::VectorXf* est_bit_count_ratios) {
    EXPECT_TRUE(
        analyzer_->ExtractEstimatedBitCountRatios(est_bit_count_ratios).ok());
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

  // Check the associated sparse matrix.
  std::ostringstream stream;
  stream << candidate_matrix().block(0, 0, kNumCohorts * kNumBloomBits,
                                     kNumCandidates);
  const char* kExpectedMatrixString =
      "0 0 0 0 0 \n"
      "0 0 0 0 0 \n"
      "1 1 0 1 0 \n"
      "0 0 0 0 1 \n"
      "1 0 1 0 0 \n"
      "0 0 0 0 0 \n"
      "0 1 0 1 1 \n"
      "0 0 1 0 0 \n"
      "0 1 0 0 0 \n"
      "1 0 0 0 0 \n"
      "0 0 0 0 0 \n"
      "0 1 0 1 0 \n"
      "0 0 0 0 1 \n"
      "1 0 1 1 0 \n"
      "0 0 0 0 1 \n"
      "0 0 1 0 0 \n"
      "0 0 0 0 0 \n"
      "1 0 0 0 1 \n"
      "0 0 0 0 0 \n"
      "0 0 1 1 0 \n"
      "1 0 0 0 0 \n"
      "0 1 0 1 1 \n"
      "0 0 1 0 0 \n"
      "0 1 0 0 0 \n";
  EXPECT_EQ(kExpectedMatrixString, stream.str());
}

// This test is identical to the previous test except that kNumBloomBits = 4
// instead of 8. The purpose of this test is to force the situation in which
// the two hash functions for a given cohort and a given candidate give the
// same value. For example below we see that for candidate 0, cohort 1, both
// hash functions yielded a 2. We want to test that the associated sparse
// matrix has a "1" in the corresponding position (in this case that is
// row 5, column 0) and does not have a "2" in that position. In other words
// we want to test that we correctly added only one entry to the list of
// triples that defined the sparse matrix and not two entries.
TEST_F(RapporAnalyzerTest, BuildCandidateMapSmallTestWithDuplicates) {
  static const uint32_t kNumCandidates = 5;
  static const uint32_t kNumCohorts = 3;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 4;

  SetAnalyzer(kNumCandidates, kNumBloomBits, kNumCohorts, kNumHashes);
  BuildCandidateMap();

  // clang-format off
  int expected_bit_indices[kNumCandidates][kNumCohorts*kNumHashes] = {
  // cihj means cohort = i and hash-index = j.
  // c0h0 c0h1 c1h0 c1h1 c2h0 c2h2
      {3,   1,   2,   2,   3,   2},  // candidate 0
      {1,   1,   0,   3,   2,   0},  // candidate 1
      {3,   0,   2,   0,   1,   0},  // candidate 2
      {1,   1,   2,   0,   2,   0},  // candidate 3
      {1,   0,   3,   1,   2,   2},  // candidate 4
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

  // Check the associated sparse matrix.
  std::ostringstream stream;
  stream << candidate_matrix().block(0, 0, kNumCohorts * kNumBloomBits,
                                     kNumCandidates);
  const char* kExpectedMatrixString =
      "1 0 1 0 0 \n"
      "0 0 0 0 0 \n"
      "1 1 0 1 1 \n"
      "0 0 1 0 1 \n"
      "0 1 0 0 1 \n"
      "1 0 1 1 0 \n"
      "0 0 0 0 1 \n"
      "0 1 1 1 0 \n"
      "1 0 0 0 0 \n"
      "1 1 0 1 1 \n"
      "0 0 1 0 0 \n"
      "0 1 1 1 0 \n";
  EXPECT_EQ(kExpectedMatrixString, stream.str());
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

// Tests the function ExtractEstimatedBitCountRatios(). We build one small
// estimated bit count ratio vector and explicitly check its values. We
// use no-randomness: p = 0, q = 1 so that the estimated bit counts are
// identical to the true bit counts.
TEST_F(RapporAnalyzerTest, ExtractEstimatedBitCountRatiosSmallNonRandomTest) {
  static const uint32_t kNumCandidates = 10;
  static const uint32_t kNumCohorts = 3;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 8;
  SetAnalyzer(kNumCandidates, kNumBloomBits, kNumCohorts, kNumHashes);
  AddObservation(0, "00001010");
  AddObservation(0, "00010010");
  AddObservation(1, "00001010");
  AddObservation(1, "00010010");
  AddObservation(1, "00100010");
  AddObservation(2, "00001010");
  AddObservation(2, "00010010");
  AddObservation(2, "00010010");
  AddObservation(2, "00100010");

  Eigen::VectorXf est_bit_count_ratios;
  ExtractEstimatedBitCountRatios(&est_bit_count_ratios);

  std::ostringstream stream;
  stream << est_bit_count_ratios.block(0, 0, kNumCohorts * kNumBloomBits, 1);

  const char* kExpectedVectorString =
      "       0\n"
      "       0\n"
      "       0\n"
      "     0.5\n"
      "     0.5\n"
      "       0\n"
      "       1\n"
      "       0\n"
      "       0\n"
      "       0\n"
      "0.333333\n"
      "0.333333\n"
      "0.333333\n"
      "       0\n"
      "       1\n"
      "       0\n"
      "       0\n"
      "       0\n"
      "    0.25\n"
      "     0.5\n"
      "    0.25\n"
      "       0\n"
      "       1\n"
      "       0";
  EXPECT_EQ(kExpectedVectorString, stream.str());
}

}  // namespace rappor
}  // namespace cobalt
