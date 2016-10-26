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

#include "algorithms/encoder/rappor_encoder.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/crypto_util/random_test_utils.h"

namespace cobalt {
namespace rappor {

using encoder::ClientSecret;

// Constructs a RapporEncoder with the given |config|, invokes
// encode() with a dummy string, and checks that the returned status is
// either kOK or kInvalidConfig, whichever is expected.
void TestRapporConfig(const RapporConfig& config,
                      Status expected_status,
                      int caller_line_number) {
  // Make a ClientSecret once and statically store the token.
  static const std::string kClientSecretToken =
      ClientSecret::GenerateNewSecret().GetToken();
  // Each time this function is invoked reconstitute the secret from the token.
  RapporEncoder encoder(config, ClientSecret::FromToken(kClientSecretToken));
  RapporObservation obs;
  EXPECT_EQ(expected_status, encoder.Encode("dummy", &obs))
      << "Invoked from line number: " << caller_line_number;
}

// A macro to invoke testRapporConfig and pass it the current line number.
#define TEST_RAPPOR_CONFIG(config, expected_status) \
    (TestRapporConfig(config, expected_status, __LINE__))

// Tests the validation of config for String RAPPOR.
TEST(RapporEncoderTest, StringRapporConfigValidation) {
  // Empty config: Invalid
  RapporConfig config;
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Add two probabilities, still Invalid
  config.set_prob_0_becomes_1(0.3);
  config.set_prob_1_stays_1(0.7);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_bloom_bits, still Invalid
  config.set_num_bloom_bits(8);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_hashes, still Invalid
  config.set_num_hashes(2);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // set num_cohorts: Valid
  config.set_num_cohorts(20);
  TEST_RAPPOR_CONFIG(config, kOK);

  // Explicitly set PRR to 0: Valid.
  config.set_prob_rr(0.0);
  TEST_RAPPOR_CONFIG(config, kOK);

  // Explicitly set PRR to non-zero: Invalid.
  config.set_prob_rr(0.1);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Explicitly set PRR back to zero: Valid.
  config.set_prob_rr(0.0);
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set one of the probabilities to negative: Invalid
  config.set_prob_0_becomes_1(-0.3);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set one of the probabilities to greater than 1: Invalid
  config.set_prob_0_becomes_1(1.3);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Fix the probability: Valid
  config.set_prob_0_becomes_1(0.3);
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set the other probability to negative: Invalid
  config.set_prob_1_stays_1(-0.7);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set the other probability to greater than 1: Invalid
  config.set_prob_1_stays_1(1.7);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Fix the probability: Valid
  config.set_prob_1_stays_1(0.7);
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set num_bloom_bits to negative: Invalid
  config.set_num_bloom_bits(-8);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_bloom_bits to 0: Invalid
  config.set_num_bloom_bits(0);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_bloom_bits back to positive: Valid
  config.set_num_bloom_bits(8);
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set num_hashes to negative: Invalid
  config.set_num_hashes(-2);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_hashes to 0: Invalid
  config.set_num_hashes(0);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_hashes to 8: Invalid
  config.set_num_hashes(8);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_hashes back to positive: Valid
  config.set_num_hashes(2);
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set num_cohorts to negative: Invalid
  config.set_num_cohorts(-20);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_cohorts to 0: Invalid
  config.set_num_cohorts(0);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_cohorts to 1024: Invalid
  config.set_num_cohorts(1024);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_cohorts back to positive: Valid
  config.set_num_cohorts(20);
  TEST_RAPPOR_CONFIG(config, kOK);

  // Set num_bloom_bits to equal num_hashes: Invalid
  config.set_num_bloom_bits(2);
  TEST_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set num_bloom_bits to greater than num_hashes: Valid
  config.set_num_bloom_bits(3);
  TEST_RAPPOR_CONFIG(config, kOK);

  // Test with an invalid ClientSecret
  RapporEncoder encoder(config, ClientSecret::FromToken("Invalid Token"));
  RapporObservation obs;
  EXPECT_EQ(kInvalidConfig, encoder.Encode("dummy", &obs));
}

// Constructs a BasicRapporEncoder with the given |config|, invokes
// encode() with a dummy string, and checks that the returned status is
// either kOK or kInvalidConfig, whichever is expected.
void TestBasicRapporConfig(const BasicRapporConfig& config,
                           Status expected_status,
                           int caller_line_number) {
  // Make a ClientSecret once and statically store the token.
  static const std::string kClientSecretToken =
      ClientSecret::GenerateNewSecret().GetToken();
  // Each time this function is invoked reconstitute the secret from the token.
  BasicRapporEncoder encoder(config,
      ClientSecret::FromToken(kClientSecretToken));
  BasicRapporObservation obs;
  EXPECT_EQ(expected_status, encoder.Encode("cat", &obs))
      << "Invoked from line number: " << caller_line_number;
}

// A macro to invoke TestBasicRapporConfig and pass it the current line number.
#define TEST_BASIC_RAPPOR_CONFIG(config, expected_status) \
    (TestBasicRapporConfig(config, expected_status, __LINE__))

// Tests the validation of config for Basic RAPPOR.
TEST(RapporEncoderTest, BasicRapporConfigValidation) {
  // Empty config: Invalid
  BasicRapporConfig config;
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Add two probabilities but no categories: Invalid
  config.set_prob_0_becomes_1(0.3);
  config.set_prob_1_stays_1(0.7);
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Add one category: Valid.
  config.add_category("cat");
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Add two more categories: Valid.
  config.add_category("dog");
  config.add_category("fish");
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Explicitly set PRR to 0: Valid.
  config.set_prob_rr(0.0);
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Explicitly set PRR to non-zero: Invalid.
  config.set_prob_rr(0.1);
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Explicitly set PRR back to zero: Valid.
  config.set_prob_rr(0.0);
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Set one of the probabilities to negative: Invalid
  config.set_prob_0_becomes_1(-0.3);
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set one of the probabilities to greater than 1: Invalid
  config.set_prob_0_becomes_1(1.3);
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Fix the probability: Valid
  config.set_prob_0_becomes_1(0.3);
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Set the other probability to negative: Invalid
  config.set_prob_1_stays_1(-0.7);
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Set the other the probability to greater than 1: Invalid
  config.set_prob_1_stays_1(1.7);
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Fix the probability: Valid
  config.set_prob_1_stays_1(0.7);
  TEST_BASIC_RAPPOR_CONFIG(config, kOK);

  // Add an empty category: Invalid
  config.add_category("");
  TEST_BASIC_RAPPOR_CONFIG(config, kInvalidConfig);

  // Test with an invalid ClientSecret
  BasicRapporEncoder encoder(config, ClientSecret::FromToken("Invalid Token"));
  BasicRapporObservation obs;
  EXPECT_EQ(kInvalidConfig, encoder.Encode("dummy", &obs));
}

// Returns whether or not the bit with the given |bit_index| is set in
// |data|. The bits are indexed "from right-to-left", i.e. from least
// significant to most significant. The least significant bit has index 0.
bool IsSet(const std::string& data, int bit_index) {
  uint32_t num_bytes = data.size();
  uint32_t byte_index = bit_index / 8;
  uint32_t bit_in_byte_index = bit_index % 8;
  return data[num_bytes - byte_index - 1] & (1 << bit_in_byte_index);
}

// Returns a string of "0"s and "1"s that gives the binary representation of the
// bytes in |data|.
std::string DataToBinaryString(const std::string& data) {
  size_t num_bits = data.size() * 8;
  // Initialize output to a string of all zeroes.
  std::string output(num_bits, '0');
  size_t output_index = 0;
  for (int bit_index = num_bits - 1; bit_index >= 0; bit_index--) {
    if (IsSet(data, bit_index)) {
      output[output_index] = '1';
    }
    output_index++;
  }
  return output;
}

// Tests the function DataToBinaryString().
TEST(UtilityTest, DataToBinaryString) {
  // One byte
  EXPECT_EQ(DataToBinaryString(std::string("\0", 1)),   "00000000");
  EXPECT_EQ(DataToBinaryString(std::string("\x1", 1)),  "00000001");
  EXPECT_EQ(DataToBinaryString(std::string("\x2", 1)),  "00000010");
  EXPECT_EQ(DataToBinaryString(std::string("\x3", 1)),  "00000011");
  EXPECT_EQ(DataToBinaryString(std::string("\xFE", 1)), "11111110");

  // Two bytes
  EXPECT_EQ(DataToBinaryString(std::string("\0\0", 2)),
      "0000000000000000");
  EXPECT_EQ(DataToBinaryString(std::string("\0\1", 2)),
      "0000000000000001");
  EXPECT_EQ(DataToBinaryString(std::string("\1\0", 2)),
      "0000000100000000");
  EXPECT_EQ(DataToBinaryString(std::string("\0\1", 2)),
      "0000000000000001");
  EXPECT_EQ(DataToBinaryString(std::string("\1\xFE", 2)),
      "0000000111111110");

  // Three bytes
  EXPECT_EQ(DataToBinaryString(std::string("\0\0\0", 3)),
      "000000000000000000000000");
  EXPECT_EQ(DataToBinaryString(std::string("\0\0\1", 3)),
      "000000000000000000000001");
  EXPECT_EQ(DataToBinaryString(std::string("\0\1\0", 3)),
      "000000000000000100000000");
  EXPECT_EQ(DataToBinaryString(std::string("\1\1\0", 3)),
      "000000010000000100000000");
}

// Builds the string "category<x><y>" where <x> and <y> are the two-digit
// representation of |index| which must be less than 100.
std::string CategoryName(int index) {
  char buffer[11];
  snprintf(buffer, sizeof(buffer), "category%02u", index);
  return std::string(buffer, 10);
}

// Returns a string of characters of length |num_bits| with |index_char| in
// position |index| and |other_char| in all other positions.
// REQUIRES: 0 <= index < num_bits.
std::string BuildBitPatternString(int num_bits, int index, char index_char,
    char other_char ) {
  return std::string(num_bits - 1 - index, other_char) + index_char +
      std::string(index, other_char);
}

// Performs a test of BasicRepporEncoder::Encode() in the two special cases that
// that there is no randomness involved in the encoded string, namely
// (a) p = 0, q = 1
// (b) p = 1, q = 0
//
// |num_categories| must be a positive integer. Basic RAPPOR will be configured
// to have this many categories. The encoding will be performed for each of
// the categores.
//
// |q_is_one| Do the test in case (a) where p = 0, q = 1
void DoBasicRapporNoRandomnessTest(int num_categories, bool q_is_one) {
  // Select the parameters based on the mode. index_char and other_char
  // determine the expected bit pattern in the encoding. index_char is the
  // character we expect to see in the position of the given category and
  // other_char is the character we expect to see in the other positions.
  float p, q;
  char index_char, other_char;
  if (q_is_one) {
    // We expect a 1 in the index position and 0's everywhere else.
    p = 0.0;
    q = 1.0;
    index_char = '1';
    other_char = '0';
  } else {
    // We expect a 0 in the index position and 1's everywhere else.
    p = 1.0;
    q = 0.0;
    index_char = '0';
    other_char = '1';
  }

  // Configure basic RAPPOR with the selected parameters.
  BasicRapporConfig config;
  config.set_prob_0_becomes_1(p);
  config.set_prob_1_stays_1(q);
  for (int i = 0; i < num_categories; i++) {
    config.add_category(CategoryName(i));
  }

  // Construct a BasicRapporEncoder.
  static const std::string kClientSecretToken =
      ClientSecret::GenerateNewSecret().GetToken();
  BasicRapporEncoder encoder(config,
      ClientSecret::FromToken(kClientSecretToken));

  // The expected number of bits in the encoding is the least multiple of 8
  // greater than or equal to num_categories.
  int expected_num_bits = 8 * (((num_categories - 1) / 8) + 1);

  // For each category, obtain the observation and check that the bit pattern
  // is as expected.
  BasicRapporObservation obs;
  for (int i = 0; i < num_categories; i++) {
    auto category_name = CategoryName(i);
    ASSERT_EQ(kOK, encoder.Encode(category_name, &obs)) << category_name;
    auto expected_pattern = BuildBitPatternString(expected_num_bits, i,
        index_char, other_char);
    EXPECT_EQ(DataToBinaryString(obs.data()), expected_pattern);
  }
}

// Performs a test of BasicRepporEncoder::Encode() in the special case that
// the values of p and q are either 0 or 1 so that there is no randomness
// involved in the encoded string.
TEST(BasicRapporEncoderTest, NoRandomness) {
  // We test with between 1 and 50 categories.
  for (int num_categories = 1; num_categories <=50; num_categories++) {
    // We test all 4 modes. See comments at DoBasicRapporNoRandomnessTest.
    DoBasicRapporNoRandomnessTest(num_categories, true);
    DoBasicRapporNoRandomnessTest(num_categories, false);
  }
}

// Base class for tests of Basic RAPPOR that use a deterministic RNG.
class BasicRapporDeterministicTest : public ::testing::Test {
 protected:
  std::unique_ptr<BasicRapporEncoder> BuildEncoder(float prob_0_becomes_1,
    float prob_1_stays_1, int num_categories) {
    // Configure BasicRappor.
    BasicRapporConfig config;
    config.set_prob_0_becomes_1(prob_0_becomes_1);
    config.set_prob_1_stays_1(prob_1_stays_1);
    for (int i = 0; i < num_categories; i++) {
      config.add_category(CategoryName(i));
    }

    // Construct a BasicRapporEncoder.
    static const std::string kClientSecretToken =
        ClientSecret::GenerateNewSecret().GetToken();
    std::unique_ptr<BasicRapporEncoder> encoder(new BasicRapporEncoder(
        config, ClientSecret::FromToken(kClientSecretToken)));

    // Give the encoder a deterministic RNG.
    encoder->SetRandomForTesting(std::unique_ptr<crypto::Random>(
        new crypto::DeterministicRandom()));

    return encoder;
  }

  // Generates a Basic RAPPOR observation 1000 times and then performs Pearson's
  // chi-squared test on each bit separately to check for goodness of fit to
  // a binomial distribution with the appropriate parameter. Fails if
  // chi-squared >= |chi_squared_threshold|.
  //
  // Uses DeterministicRandom in order to ensure reproducibility.
  //
  // REQUIRES: 0 <= selected_category < num_categories.
  // All 1000 of the observations will be for the selected category. Thus
  // the expected number of 1's in the bit position corresponding to the
  // selected category is prob_1_stays_1 and the expected number of 1'1 in
  // all other bit positions is prob_0_becomes_1.
  void DoChiSquaredTest(float prob_0_becomes_1, float prob_1_stays_1,
      int num_categories, int selected_category, double chi_squared_threshold) {
    // Build the encoder
    auto encoder = BuildEncoder(prob_0_becomes_1, prob_1_stays_1,
        num_categories);

    // Sample 1000 observations of the selected category and collect the bit
    // counts
    static const int kNumTrials = 1000;
    auto category_name = CategoryName(selected_category);
    BasicRapporObservation obs;
    std::vector<int> counts(num_categories, 0);
    for (size_t i = 0; i < kNumTrials; i++) {
      obs.Clear();
      EXPECT_EQ(kOK, encoder->Encode(category_name, &obs));
      for (int bit_index = 0; bit_index < num_categories; bit_index++) {
        if (IsSet(obs.data(), bit_index)) {
          counts[bit_index]++;
        }
      }
    }

    // In the special case where prob_1_stays_1 is 1 make sure that we got
    // 1000 1's in the selected category.
    if (prob_1_stays_1 == 1.0) {
      EXPECT_EQ(kNumTrials, counts[selected_category]);
    }

    // This is the expected number of ones and zeroes for the bit position in
    // the selected category.
    const double expected_1_selected =
        static_cast<double>(kNumTrials) * prob_1_stays_1;
    const double expected_0_selected =
        static_cast<double>(kNumTrials) - expected_1_selected;

    // This is the expected number of ones and zeroes for all bit positions
    // other than the selected category.
    const double expected_1 =
        static_cast<double>(kNumTrials) * prob_0_becomes_1;
    const double expected_0 =
        static_cast<double>(kNumTrials) - expected_1;

    // For each of the bit positions, perform the chi-squared test.
    for (int bit_index = 0; bit_index < num_categories; bit_index++) {
      double exp_0 =
          (bit_index == selected_category ? expected_0_selected : expected_0);
      double exp_1 =
          (bit_index == selected_category ? expected_1_selected : expected_1);

      if (exp_0 != 0.0 && exp_1 != 0.0) {
        // Difference between actual 1 count and expected 1 count.
        double delta_1 = static_cast<double>(counts[bit_index]) - exp_1;

        // Difference between actual 0 count and expected 0 count.
        double delta_0 =
            static_cast<double>(kNumTrials - counts[bit_index]) - exp_0;

        // Compute and check the Chi-Squared value.
        double chi_squared =
          delta_1*delta_1/exp_1 + delta_0*delta_0/exp_0;

        EXPECT_TRUE(chi_squared < chi_squared_threshold) << "chi_squared=" <<
            chi_squared << " chi_squared_threshold=" << chi_squared_threshold <<
            " bit_index=" << bit_index << " delta_0=" << delta_0 <<
            " delta_1=" <<delta_1 << " num_categories=" << num_categories <<
            " selected_category=" << selected_category <<
            " prob_0_becomes_1=" << prob_0_becomes_1 <<
            " prob_1_stays_1=" << prob_1_stays_1;
      }
    }
  }
};

TEST_F(BasicRapporDeterministicTest, ChiSquaredTest) {
  // Perform the chi-squared test for various numbers of categories and
  // various selected categories. This gets combinatorially explosive so to
  // keep the testing time reasonable we don't test every combination but
  // rather step through the num_categories by 7 and use at most 3 selected
  // categories for each num_categories.
  for (int num_categories = 1; num_categories < 40; num_categories+=7) {
    for (int selected_category = 0; selected_category < num_categories;
        selected_category+=(num_categories/3 + 1)) {
      // The first two parameters are p and q.
      //
      // The last parameter is the chi-squared value to use. Notice that these
      // values were chosen by experimentation to be as small as possible so
      // that the test passes. They do not necessarily correspond to natural
      // confidence intervals for the chi-squared test.
      DoChiSquaredTest(0.01, 0.99, num_categories, selected_category, 8.2);
      DoChiSquaredTest(0.1, 0.9, num_categories, selected_category, 9.4);
      DoChiSquaredTest(0.2, 0.8, num_categories, selected_category, 11.1);
      DoChiSquaredTest(0.25, 0.75, num_categories, selected_category, 11.8);
      DoChiSquaredTest(0.3, 0.7, num_categories, selected_category, 11.8);
    }
  }
}

// Test that BasicRapporEncoder::Encode() returns kInvalidArgument if a category
// name is used that is not one of the registered categories.
TEST(BasicRapporEncoderTest, BadCategory) {
  // Configure Basic RAPPOR with two categories, "dog" and "cat".
  BasicRapporConfig config;
  config.set_prob_0_becomes_1(0.3);
  config.set_prob_1_stays_1(0.7);
  config.add_category("dog");
  config.add_category("cat");

  // Construct a BasicRapporEncoder.
  static const std::string kClientSecretToken =
      ClientSecret::GenerateNewSecret().GetToken();
  BasicRapporEncoder encoder(config,
      ClientSecret::FromToken(kClientSecretToken));

  // Attempt to encode a string that is not one of the categories. Expect
  // to receive kInvalidInput.
  BasicRapporObservation obs;
  EXPECT_EQ(kInvalidInput, encoder.Encode("fish", &obs));
}

}  // namespace rappor
}  // namespace cobalt

