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
#include "util/crypto_util/random.h"

#include <bitset>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/crypto_util/random_test_utils.h"

namespace cobalt {
namespace crypto {

// Returns the number of bits of the byte x that are set to one.
int NumBitsSet(byte x) {
  std::bitset<8> bit_set(x);
  return bit_set.count();
}

// Performs the experiment of invoking RandomBits(p) 100 times and
// returns the average number of bits set. Uses DeterministicRandom in order to
// ensure reproducibility.
int AverageNumBitsSet(float p) {
  std::unique_ptr<Random> rand(new DeterministicRandom());
  int cumulative_num_bits_set = 0;
  for (int i = 0; i < 100; i++) {
    cumulative_num_bits_set += NumBitsSet(rand->RandomBits(p));
  }
  return round(cumulative_num_bits_set / 100.0);
}

// Invokes RandomBits(p) 1000 times, collects the sum of the values for each
// of the 8 bits separately, and then performs Pearson's chi-squared
// test on each bit separately to check for goodness of fit to a binomial
// distribution with parameter p. Fails if chi-squared >= 5.024 which
// corresponds to rejecting the null hypothesis with confidence 0.975.
// Uses DeterministicRandom in order to ensure reproducibility.
void DoChiSquaredTest(float p) {
  // Do the experiment and collet the counts.
  std::unique_ptr<Random> rand(new DeterministicRandom());
  std::vector<int> counts(8, 0);
  static const int kNumTrials = 1000;
  for (int i = 0; i < kNumTrials; i++) {
    std::bitset<8> bit_set(rand->RandomBits(p));
    for (size_t j = 0; j < 8 ; j++) {
      counts[j] += bit_set[j];
    }
  }

  // Check the errors.
  const double expected_1 = static_cast<double>(kNumTrials) * p;
  const double expected_0 = static_cast<double>(kNumTrials) - expected_1;
  for (size_t j = 0; j < 8 ; j++) {
    // Difference between actual 1 count and expected 1 count.
    double delta_1 = static_cast<double>(counts[j]) - expected_1;

    // Difference between actual 0 count and expected 0 count.
    double delta_0 = static_cast<double>(kNumTrials - counts[j]) - expected_0;

    // Compute and check the Chi-Squared value.
    double chi_squared =
        delta_1*delta_1/expected_1 + delta_0*delta_0/expected_0;

    // The number 5.024 below has the property that
    // P(X < 5.024) = 0.975 where X ~ chi^2(1)
    EXPECT_TRUE(chi_squared < 5.024);
  }
}

// Tests the function RandomBits()
TEST(RandomTest, TestRandomBits) {
  std::unique_ptr<Random> rand(new Random());

  // When p = 0 none of the bits should be set.
  EXPECT_EQ(0, rand->RandomBits(0.0));

  // When p = 1 all of the bits should be set.
  EXPECT_EQ(255, rand->RandomBits(1.0));

  // For the remainder of the test we switch to using DeterministicRandom
  // so that the test is not flaky.

  // When p = i/8 then on average i of the bits should be set.
  for (int i = 1; i <= 7; i++) {
    double p = static_cast<double>(i/8.0);
    EXPECT_EQ(i, AverageNumBitsSet(p));
  }

  // Pick some other values for p and both test the average number of bits
  // set and also perform a Chi-squared test.
  static const std::pair<float, int> expected_averages[] =
      {{0.1,  1},
       {0.2,  2},
       {0.25, 2},
       {0.3,  2},
       {0.4,  3},
       {0.5,  4},
       {0.6,  5},
       {0.7,  6},
       {0.75, 6},
       {0.8,  7},
       {0.9,  7},
       {0.95, 8}};
  for (auto pair : expected_averages) {
    EXPECT_EQ(pair.second, AverageNumBitsSet(pair.first));

    DoChiSquaredTest(pair.first);
  }
}

}  // namespace crypto

}  // namespace cobalt

