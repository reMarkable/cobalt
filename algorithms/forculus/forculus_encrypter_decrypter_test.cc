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

#include "algorithms/analyzer/forculus_decrypter.h"
#include "algorithms/encoder/forculus_encrypter.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace forculus {

using encoder::ClientSecret;
using util::CalendarDate;

void TestEncrypterValidation(uint32_t threshold, bool use_valid_date,
    bool use_valid_token,
    ForculusEncrypter::Status expected_status,
    int caller_line_number) {
  // Make a ClientSecret once and statically store the token.
  static const std::string kClientSecretToken =
      ClientSecret::GenerateNewSecret().GetToken();

  std::string client_secret_token =
      (use_valid_token ? kClientSecretToken : "Invalid Token");

  // Make a config with the given threshold
  ForculusConfig config;
  config.set_threshold(threshold);

  // Construct the Encrypter.
  ForculusEncrypter encrypter(config, ClientSecret::FromToken(
      client_secret_token));

  // Construct a valid or invalid observation date.
  CalendarDate obs_date;
  if (!use_valid_date) {
    obs_date.year = 0;
  }

  // Invoke Encrypt() and check the status.
  ForculusObservation obs;
  EXPECT_EQ(expected_status,
    encrypter.Encrypt("hello", obs_date, &obs))
     << "Invoked from line number: " << caller_line_number;
}

// A macro to invoke TestEncrypterValidation and pass it the current line num.
#define TEST_ENCRYPTER_VALIDATION(threshold, use_valid_date, use_valid_token, \
                                  expected_status) \
    (TestEncrypterValidation(threshold, use_valid_date, use_valid_token, \
     expected_status, __LINE__))

// Tests ForculusEncrypter config and input validation.
TEST(ForculusEncrypterTest, Validation) {
  bool use_valid_date = true;
  bool use_valid_token = true;

  // threshold = 1: kInvalidConfig
  TEST_ENCRYPTER_VALIDATION(1, use_valid_date, use_valid_token,
      ForculusEncrypter::kInvalidConfig);

  // threshold = 1 with invalid date: kInvalidConfig
  use_valid_date = false;
  TEST_ENCRYPTER_VALIDATION(1, use_valid_date, use_valid_token,
      ForculusEncrypter::kInvalidConfig);

  // threshold = 2 with invalid date: kInvalidInput
  TEST_ENCRYPTER_VALIDATION(2, use_valid_date, use_valid_token,
      ForculusEncrypter::kInvalidInput);

  // threshold = 2 with valid date: kOK
  use_valid_date = true;
  TEST_ENCRYPTER_VALIDATION(2, use_valid_date, use_valid_token,
      ForculusEncrypter::kOK);

  // threshold = UINT32_MAX: kInvalidConfig
  TEST_ENCRYPTER_VALIDATION(UINT32_MAX, use_valid_date, use_valid_token,
      ForculusEncrypter::kInvalidConfig);

  // threshold = 1000: kOK
  TEST_ENCRYPTER_VALIDATION(1000, use_valid_date, use_valid_token,
      ForculusEncrypter::kOK);

  // invalid token: kInvalidConfig
  use_valid_token = false;
  TEST_ENCRYPTER_VALIDATION(1000, use_valid_date, use_valid_token,
      ForculusEncrypter::kInvalidConfig);
}

}  // namespace forculus

}  // namespace cobalt

