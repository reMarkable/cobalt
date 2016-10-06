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

#ifndef COBALT_UTIL_CRYPTO_UTIL_CIPHER_H_
#define COBALT_UTIL_CRYPTO_UTIL_CIPHER_H_

#include <memory>
#include <vector>

#include "util/crypto_util/types.h"

namespace cobalt {

namespace crypto {

class CipherContext;


// Provides a C++ interface to an AEAD implementation.
//
// An instance of SymmetricCipher may be used repeatedly for multiple
// encryptions or decryptions. The method setKey() must be invoked before
// any other methods or the other operations will fail or crash.
class SymmetricCipher {
 public:
  static const size_t KEY_SIZE = 32;
  static const size_t NONCE_SIZE = 16;

  SymmetricCipher();
  ~SymmetricCipher();

  // Sets the secret key for encryption or decryption. This must be invoked
  // at least once before an instance of DeterministicCipher may be used.
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  // |key| Must have length |KEY_SIZE|.
  bool setKey(const byte key[KEY_SIZE]);

  // Performs AEAD encryption.
  //
  // |nonce| must have length |NONCE_SIZE|. It is essential that the same
  // (key, nonce) pair never be used to encrypt two different plain texts.
  // If re-using the same key multiple times you *must* change the nonce
  // or the resulting encryption will not be secure.
  //
  // |ptext| The plain text to be encrypted.
  //
  // |ptext_len| The number of bytes of plain text.
  //
  // |ctext| A pointer to a vector that will be modified to contain
  // the ciphertext.
  //
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  bool encrypt(const byte nonce[NONCE_SIZE], const byte *ptext, int ptext_len,
      std::vector<byte>* ctext);

  // Performs AEAD decryption.
  //
  // |nonce| must have length |NONCE_SIZE|.
  //
  // |ctext| The ciphertext to be decrypted.
  //
  // |ctext_len| The number of bytes of ciphertext.
  //
  // |rtext| A pointer to a vector that will be modified to contain the
  // recovered plaintext.
  //
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  bool decrypt(const byte nonce[NONCE_SIZE], const byte *ctext, int ctext_len,
      std::vector<byte>* rtext);

 private:
  std::unique_ptr<CipherContext> context_;
};

}  // namespace crypto

}  // namespace cobalt

#endif  // COBALT_UTIL_CRYPTO_UTIL_CIPHER_H_












































































