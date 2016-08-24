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

#include "util/crypto_util/cipher.h"

#include <limits.h>
#include <string>
#include <vector>

#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/crypto_util/errors.h"
#include "util/crypto_util/random.h"

namespace cobalt {
namespace crypto {

using byte = SymmetricCipher::byte;

const char* kLine1 = "The woods are lovely, dark and deep,\n";
const char* kLine2 = "But I have promises to keep,\n";
const char* kLine3 = "And miles to go before I sleep,\n";
const char* kLine4 = "And miles to go before I sleep.";
const char* kLines[] = {kLine1, kLine2, kLine3, kLine4};
const int kNumLines = 4;

// Tests SymmetricCipher().

// This function is invoked by the SymmetricCipherTest.
// It encrypts and then decrypts |plain_text| and checks that the
// recovered text is equal to the plain text. It generates a random
// key and nonce.
void doSymmetricCipherTest(SymmetricCipher* cipher,
    const byte *plain_text, int ptext_len) {
  // Initialize
  byte key[SymmetricCipher::KEY_SIZE];
  byte nonce[SymmetricCipher::NONCE_SIZE];
  Random_Bytes(key, SymmetricCipher::KEY_SIZE);
  Random_Bytes(nonce, SymmetricCipher::NONCE_SIZE);
  EXPECT_TRUE(cipher->setKey(key)) << GetLastErrorMessage();

  // Encrypt
  std::vector<byte> cipher_text;
  EXPECT_TRUE(cipher->encrypt(nonce, plain_text, ptext_len, &cipher_text))
      << GetLastErrorMessage();

  // Decrypt
  std::vector<byte> recovered_text;
  EXPECT_TRUE(cipher->decrypt(nonce, cipher_text.data(), cipher_text.size(),
      &recovered_text)) << GetLastErrorMessage();

  // Compare
  EXPECT_EQ(std::string((const char*)recovered_text.data(),
                        recovered_text.size()),
            std::string((const char*)plain_text));
}

TEST(SymmetricCipherTest, TestManyStrings) {
  SymmetricCipher cipher;

  // Test once with each line separately.
  for (int i = 0; i < kNumLines; i++) {
    doSymmetricCipherTest(&cipher, (const byte*) kLines[i],
                             strlen(kLines[i]));
  }

  // Test once with all lines together.
  std::string all_lines;
  for (int i = 0; i < kNumLines; i++) {
    all_lines += kLines[i];
  }
  doSymmetricCipherTest(&cipher, (const byte*) all_lines.data(),
                           all_lines.size());

  // Test once with a longer string: Repeat string 32 times.
  for (int i = 0; i < 5; i++) {
    all_lines += all_lines;
  }
  doSymmetricCipherTest(&cipher, (const byte*) all_lines.data(),
                           all_lines.size());
}

}  // namespace crypto

}  // namespace cobalt

