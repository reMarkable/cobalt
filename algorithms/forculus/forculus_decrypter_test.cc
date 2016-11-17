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

#include "algorithms/forculus/forculus_decrypter.h"
#include "algorithms/forculus/forculus_encrypter.h"

#include <map>

#include "encoder/client_secret.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/datetime_util.h"

namespace cobalt {
namespace forculus {

using encoder::ClientSecret;
using util::CalendarDate;

static const uint32_t kThreshold = 20;

// Encrypts the plaintext using Forculus encryption with threshold = kThreshold
// and default values for the other parameters and a fresh ClientSecret will
// be generated each time this function is invoked..
ForculusObservation Encrypt(const std::string& plaintext) {
  // Make a config with the given threshold
  ForculusConfig config;
  config.set_threshold(kThreshold);

  // Construct an Encrypter.
  ForculusEncrypter encrypter(config, 0, 0, 0, "",
      ClientSecret::GenerateNewSecret());

  // Invoke Encrypt() and check the status.
  ForculusObservation obs;
  EXPECT_EQ(ForculusEncrypter::kOK,
      encrypter.Encrypt(plaintext, CalendarDate(), &obs));
  return obs;
}

// Simulates kThreshold different clients generating ciphertexts for the
// same plaintext. Verifies that the plaintext will be properly decrypted.
TEST(ForculusDecrypterTest, TestSuccessfulDecryption) {
  const std::string plaintext("The woods are lovely, dark and deep.");
  ForculusDecrypter* decrypter = nullptr;
  for (int i = 0; i < kThreshold; i++) {
    auto observation = Encrypt(plaintext);
    if (!decrypter) {
      decrypter = new ForculusDecrypter(kThreshold, observation.ciphertext());
    } else {
      EXPECT_EQ(decrypter->ciphertext(), observation.ciphertext());
    }
    decrypter->AddObservation(observation);
  }
  std::string recovered_text;
  EXPECT_EQ(ForculusDecrypter::kOK, decrypter->Decrypt(&recovered_text));
  EXPECT_EQ(plaintext, recovered_text);
}

// Verifies that ForculusDecrypter returns appropriate error statuses.
TEST(ForculusDecrypterTest, TestErrors) {
  // Construct Observation 1.
  ForculusObservation obs1;
  obs1.set_ciphertext("A ciphertext");
  obs1.set_point_x("12345");
  obs1.set_point_y("abcde");

  // Construct Observation 2 with the same ciphertext and the same x-value
  // but a different y-value.
  ForculusObservation obs2;
  obs2.set_ciphertext("A ciphertext");
  obs2.set_point_x("12345");
  obs2.set_point_y("fghij");

  // Construct a decrypter with the same ciphertext and a threshold of 3.
  ForculusDecrypter decrypter(3, "A ciphertext");
  EXPECT_EQ("A ciphertext", decrypter.ciphertext());

  // It is ok to add the same observation twice. It will be ignored the
  // second time.
  EXPECT_EQ(ForculusDecrypter::kOK, decrypter.AddObservation(obs1));
  EXPECT_EQ(ForculusDecrypter::kOK, decrypter.AddObservation(obs1));
  EXPECT_EQ(1, decrypter.size());

  // Trying to add Obervation 2 will yield kInconsistentPoints.
  EXPECT_EQ(ForculusDecrypter::kInconsistentPoints,
      decrypter.AddObservation(obs2));

  // Trying to decrypt now will yield kNontEnoughPoints.
  std::string plaintext;
  EXPECT_EQ(ForculusDecrypter::kNotEnoughPoints, decrypter.Decrypt(&plaintext));

  // Change Observation 2 to have a different x-value and a different
  // ciphertext. Now trying to add it yields kWrongCiphertext.
  obs2.set_ciphertext("A different ciphertext");
  obs2.set_point_x("23456");
  EXPECT_EQ(ForculusDecrypter::kWrongCiphertext,
      decrypter.AddObservation(obs2));

  // Fix observation 2 and we can successfully add it.
  obs2.set_ciphertext("A ciphertext");
  obs2.set_point_x("23456");
  EXPECT_EQ(ForculusDecrypter::kOK, decrypter.AddObservation(obs2));
  EXPECT_EQ(2, decrypter.size());

  // Still not enough points.
  EXPECT_EQ(ForculusDecrypter::kNotEnoughPoints, decrypter.Decrypt(&plaintext));

  // Change observation 2 to a third point and add it.
  obs2.set_ciphertext("A ciphertext");
  obs2.set_point_x("45678");
  EXPECT_EQ(ForculusDecrypter::kOK, decrypter.AddObservation(obs2));
  EXPECT_EQ(3, decrypter.size());

  // Now there are enough points to try to decrypt but the decryption will
  // fail because the ciphertext is not a real ciphertext.
  EXPECT_EQ(ForculusDecrypter::kDecryptionFailed,
      decrypter.Decrypt(&plaintext));
}

}  // namespace forculus
}  // namespace cobalt

