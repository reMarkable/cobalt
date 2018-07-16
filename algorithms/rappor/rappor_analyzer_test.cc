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
#include "third_party/eigen/Eigen/SparseQR"
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

  void AddObservationsForCandidates(const std::vector<int>& candidate_indices) {
    for (auto index : candidate_indices) {
      // Construct a new encoder with a new ClientSecret so that a random
      // cohort is selected.
      RapporEncoder encoder(config_, ClientSecret::GenerateNewSecret());

      // Encode the current candidate string using |encoder|.
      ValuePart value_part;
      value_part.set_string_value(CandidateString(index));
      RapporObservation observation;
      encoder.Encode(value_part, &observation);
      EXPECT_TRUE(analyzer_->AddObservation(observation));
    }
  }

  void PrintTrueCountsAndEstimates(
      const std::string& case_label, uint32_t num_candidates,
      const std::vector<CandidateResult>& results,
      const std::vector<int>& true_candidate_counts) {
    std::vector<int> count_estimates(num_candidates);
    for (size_t i = 0; i < num_candidates; i++) {
      count_estimates[i] = static_cast<int>(round(results[i].count_estimate));
    }
    std::ostringstream true_stream;
    for (auto x : true_candidate_counts) {
      true_stream << x << " ";
    }
    LOG(ERROR) << "-------------------------------------";
    LOG(ERROR) << case_label;
    LOG(ERROR) << "True counts: " << true_stream.str();
    std::ostringstream estimate_stream;
    for (auto x : count_estimates) {
      estimate_stream << x << " ";
    }
    LOG(ERROR) << "  Estimates: " << estimate_stream.str();
  }

  // Computes the least squares fit on the candidate matrix using QR,
  // for the given rhs in |est_bit_count_ratios| and saves it to |results|
  grpc::Status ComputeLeastSquaresFitQR(
      const Eigen::VectorXf& est_bit_count_ratios,
      std::vector<CandidateResult>* results) {
    // cast from smaller to larger type for comparisons
    const size_t num_candidates =
        static_cast<const size_t>(analyzer_->candidate_matrix_.cols());
    EXPECT_EQ(results->size(), num_candidates);
    // define the QR solver and perform the QR decomposition followed by
    // least squares solve
    Eigen::SparseQR<Eigen::SparseMatrix<float, Eigen::ColMajor>,
                    Eigen::COLAMDOrdering<int>>
        qrsolver;

    EXPECT_EQ(analyzer_->candidate_matrix_.rows(), est_bit_count_ratios.size());
    EXPECT_GT(analyzer_->candidate_matrix_.rows(), 0);
    // explicitly construct Eigen::ColMajor matrix from candidate_matrix_
    // (the documentation for Eigen::SparseQR requires it)
    // compute() as well as Eigen::COLAMDOrdering require compressed
    // matrix
    Eigen::SparseMatrix<float, Eigen::ColMajor> candidate_matrix_col_major =
        analyzer_->candidate_matrix_;
    candidate_matrix_col_major.makeCompressed();
    qrsolver.compute(candidate_matrix_col_major);
    if (qrsolver.info() != Eigen::Success) {
      std::string message = "Eigen::SparseQR decomposition was unsuccessfull";
      return grpc::Status(grpc::INTERNAL, message);
    }
    Eigen::VectorXf result_vals = qrsolver.solve(est_bit_count_ratios);
    if (qrsolver.info() != Eigen::Success) {
      std::string message = "Eigen::SparseQR solve was unsuccessfull";
      return grpc::Status(grpc::INTERNAL, message);
    }

    // write to the results vector
    EXPECT_EQ(num_candidates, results->size());
    for (size_t i = 0; i < num_candidates; i++) {
      results->at(i).count_estimate =
          result_vals[i] * analyzer_->bit_counter_.num_observations();
      results->at(i).std_error = 0;
    }
    return grpc::Status::OK;
  }

  // Runs a simple least squares problem for Ax = b on the candidate matrix
  // using QR algorithm from eigen library; this is to see the results
  // without penalty terms (note: in an overdetermined system the solution
  // is not unique so this is more a helper testing function to cross-check
  // the behavior of regression without penalty)
  void RunSimpleLinearRegressionReference(
      const std::string& case_label, uint32_t num_candidates,
      uint32_t num_bloom_bits, uint32_t num_cohorts, uint32_t num_hashes,
      std::vector<int> candidate_indices,
      std::vector<int> true_candidate_counts) {
    SetAnalyzer(num_candidates, num_bloom_bits, num_cohorts, num_hashes);
    AddObservationsForCandidates(candidate_indices);

    // set up the matrix
    auto status = analyzer_->BuildCandidateMap();
    if (!status.ok()) {
      EXPECT_EQ(grpc::OK, status.error_code());
      return;
    }
    // set up the right hand side of the equation
    Eigen::VectorXf est_bit_count_ratios;
    status = analyzer_->ExtractEstimatedBitCountRatios(&est_bit_count_ratios);
    if (!status.ok()) {
      EXPECT_EQ(grpc::OK, status.error_code());
      return;
    }

    std::vector<CandidateResult> results(num_candidates);
    status = ComputeLeastSquaresFitQR(est_bit_count_ratios, &results);
    if (!status.ok()) {
      EXPECT_EQ(grpc::OK, status.error_code());
      return;
    }

    PrintTrueCountsAndEstimates(case_label, num_candidates, results,
                                true_candidate_counts);
  }

  // Invokes the Analyze() method using the given parameters. Checks that
  // the algorithms converges and that the result vector has the correct length.
  // Doesn't check the result vector at all but uses LOG(ERROR) statments
  // to print the true candidate counts and the computed estimates to the
  // console for the sake of experimentation.
  void DoExperimentWithAnalyze(const std::string& case_label,
                               uint32_t num_candidates, uint32_t num_bloom_bits,
                               uint32_t num_cohorts, uint32_t num_hashes,
                               std::vector<int> candidate_indices,
                               std::vector<int> true_candidate_counts) {
    SetAnalyzer(num_candidates, num_bloom_bits, num_cohorts, num_hashes);
    AddObservationsForCandidates(candidate_indices);

    std::vector<CandidateResult> results;
    auto status = analyzer_->Analyze(&results);
    if (!status.ok()) {
      EXPECT_EQ(grpc::OK, status.error_code());
      return;
    }

    if (results.size() != num_candidates) {
      EXPECT_EQ(num_candidates, results.size());
      return;
    }
    PrintTrueCountsAndEstimates(case_label, num_candidates, results,
                                true_candidate_counts);
  }

  // Does the same as DoExperimentWithAnalyze except it also
  // computes the estimates for both Analyze and simple regression using QR,
  // which is computed on exactly the same system.
  void CompareAnalyzeToSimpleRegression(
      const std::string& case_label, uint32_t num_candidates,
      uint32_t num_bloom_bits, uint32_t num_cohorts, uint32_t num_hashes,
      std::vector<int> candidate_indices,
      std::vector<int> true_candidate_counts) {
    SetAnalyzer(num_candidates, num_bloom_bits, num_cohorts, num_hashes);
    AddObservationsForCandidates(candidate_indices);

    // compute and print the results of Analyze()
    std::vector<CandidateResult> results_analyze;
    auto status = analyzer_->Analyze(&results_analyze);

    if (!status.ok()) {
      EXPECT_EQ(grpc::OK, status.error_code());
      return;
    }

    if (results_analyze.size() != num_candidates) {
      EXPECT_EQ(num_candidates, results_analyze.size());
      return;
    }

    std::string print_label = case_label + " analyze ";
    PrintTrueCountsAndEstimates(print_label, num_candidates, results_analyze,
                                true_candidate_counts);

    // compute and print the results for simple linear regression
    Eigen::VectorXf est_bit_count_ratios;
    status = analyzer_->ExtractEstimatedBitCountRatios(&est_bit_count_ratios);
    if (!status.ok()) {
      EXPECT_EQ(grpc::OK, status.error_code());
      return;
    }

    print_label = case_label + " least squares ";
    std::vector<CandidateResult> results_ls(num_candidates);
    status = ComputeLeastSquaresFitQR(est_bit_count_ratios, &results_ls);
    if (!status.ok()) {
      EXPECT_EQ(grpc::OK, status.error_code());
      return;
    }
    PrintTrueCountsAndEstimates(print_label, num_candidates, results_ls,
                                true_candidate_counts);
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
// [0, num_bloom_bits). The latter two checks occur inside of
// BuildCandidateMap.
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

// This is not really a test so much as an experiment with the Analyze() method.
// It invokes Analyze() in a few very simple cases, checks that the the
// algorithm converges and that the result vector has the correct size. Then
// it prints out the true candidate counts and the computed estimates.
TEST_F(RapporAnalyzerTest, ExperimentWithAnalyze) {
  static const uint32_t kNumCandidates = 10;
  static const uint32_t kNumCohorts = 3;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 8;

  std::vector<int> candidate_indices(100, 5);
  std::vector<int> true_candidate_counts = {0, 0, 0, 0, 0, 100, 0, 0, 0, 0};
  DoExperimentWithAnalyze("p=0, q=1, only candidate 5", kNumCandidates,
                          kNumBloomBits, kNumCohorts, kNumHashes,
                          candidate_indices, true_candidate_counts);

  candidate_indices = std::vector<int>(20, 1);
  candidate_indices.insert(candidate_indices.end(), 20, 4);
  candidate_indices.insert(candidate_indices.end(), 60, 9);
  true_candidate_counts = {0, 20, 0, 0, 20, 0, 0, 0, 0, 60};
  DoExperimentWithAnalyze("p=0, q=1, several candidates", kNumCandidates,
                          kNumBloomBits, kNumCohorts, kNumHashes,
                          candidate_indices, true_candidate_counts);

  prob_0_becomes_1_ = 0.1;
  prob_1_stays_1_ = 0.9;

  candidate_indices = std::vector<int>(100, 5);
  true_candidate_counts = {0, 0, 0, 0, 0, 100, 0, 0, 0, 0};
  DoExperimentWithAnalyze("p=0.1, q=0.9, only candidate 5", kNumCandidates,
                          kNumBloomBits, kNumCohorts, kNumHashes,
                          candidate_indices, true_candidate_counts);

  candidate_indices = std::vector<int>(20, 1);
  candidate_indices.insert(candidate_indices.end(), 20, 4);
  candidate_indices.insert(candidate_indices.end(), 60, 9);
  true_candidate_counts = {0, 20, 0, 0, 20, 0, 0, 0, 0, 60};
  DoExperimentWithAnalyze("p=0.1, q=0.9, several candidates", kNumCandidates,
                          kNumBloomBits, kNumCohorts, kNumHashes,
                          candidate_indices, true_candidate_counts);
}

// Comparison of Analyze and simple least squares
// It invokes Analyze() in a few very simple cases, checks that the the
// algorithm converges and that the result vector has the correct size. For each
// case, it also computes the least squares solution using QR for exactly the
// same system and prints both solutions (note that the least squares solution
// is not always unique)
TEST_F(RapporAnalyzerTest, CompareAnalyzeToRegression) {
  static const uint32_t kNumCandidates = 10;
  static const uint32_t kNumCohorts = 3;
  static const uint32_t kNumHashes = 2;
  static const uint32_t kNumBloomBits = 8;

  std::vector<int> candidate_indices(100, 5);
  std::vector<int> true_candidate_counts = {0, 0, 0, 0, 0, 100, 0, 0, 0, 0};
  CompareAnalyzeToSimpleRegression("p=0, q=1, only candidate 5", kNumCandidates,
                                   kNumBloomBits, kNumCohorts, kNumHashes,
                                   candidate_indices, true_candidate_counts);

  candidate_indices = std::vector<int>(20, 1);
  candidate_indices.insert(candidate_indices.end(), 20, 4);
  candidate_indices.insert(candidate_indices.end(), 60, 9);
  true_candidate_counts = {0, 20, 0, 0, 20, 0, 0, 0, 0, 60};
  CompareAnalyzeToSimpleRegression(
      "p=0, q=1, several candidates", kNumCandidates, kNumBloomBits,
      kNumCohorts, kNumHashes, candidate_indices, true_candidate_counts);

  prob_0_becomes_1_ = 0.1;
  prob_1_stays_1_ = 0.9;

  candidate_indices = std::vector<int>(100, 5);
  true_candidate_counts = {0, 0, 0, 0, 0, 100, 0, 0, 0, 0};
  CompareAnalyzeToSimpleRegression(
      "p=0.1, q=0.9, only candidate 5", kNumCandidates, kNumBloomBits,
      kNumCohorts, kNumHashes, candidate_indices, true_candidate_counts);

  candidate_indices = std::vector<int>(20, 1);
  candidate_indices.insert(candidate_indices.end(), 20, 4);
  candidate_indices.insert(candidate_indices.end(), 60, 9);
  true_candidate_counts = {0, 20, 0, 0, 20, 0, 0, 0, 0, 60};
  CompareAnalyzeToSimpleRegression(
      "p=0.1, q=0.9, several candidates", kNumCandidates, kNumBloomBits,
      kNumCohorts, kNumHashes, candidate_indices, true_candidate_counts);
}

}  // namespace rappor
}  // namespace cobalt
