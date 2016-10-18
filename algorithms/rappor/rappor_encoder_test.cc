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
  EXPECT_EQ(expected_status, encoder.Encode("dummy", &obs))
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

}  // namespace rappor

}  // namespace cobalt

