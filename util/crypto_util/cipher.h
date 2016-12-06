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
class HybridCipherContext;


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
  // at least once before an instance of SymmetricCipher may be used.
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  // |key| Must have length |KEY_SIZE|.
  //
  // TODO(rudominer): setKey --> SetKey everywhere.
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
  // |ptext| A pointer to a vector that will be modified to contain the
  // recovered plaintext.
  //
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  bool decrypt(const byte nonce[NONCE_SIZE], const byte *ctext, int ctext_len,
      std::vector<byte>* ptext);

 private:
  std::unique_ptr<CipherContext> context_;
};

// Provides a C++ interface to a public-key hybrid encryption using ECDH. The
// algorithm implemented is as follows:
//
// Public key = g^x in an elliptic curve group (NIST P256) represented in
// X9.62 serialization with a compressed point repsentation.
//
// Private key = x stored in bytes and interpreted as a big-endian number (as
// in the interface of BN_bin2bn in boringssl)
//
// Enc(public key, message):
//
//    1. Samples a fresh EC keypair (g^y, y)
//    2. Samples a salt
//    3. Computes symmetric key = HKDF(g^xy, salt) with SHA256 compression
//    function
//    4. Chooses a fresh nonce of size SymmetricCipher::NONCE_SIZE
//    5. (Symmetric) encrypts message using SymmetricCipher::encrypt with key
//    and nonce computed above into ciphertext
//    6. Publishes (public_key_part, salt, nonce, ciphertext) as the hybrid
//    ciphertext, where public_key_part is the X9.62 serialization of g^y.
//
// Dec(private key, hybrid ciphertext = (public_key_part, salt, nonce,
//        ciphertext)):
//
//    1. Computes symmetric key = HKDF(g^xy, salt) with SHA256 compression
//    function from private key (x) and public_key_part (g^y)
//    2. (Symmetric) decrypts ciphertext using SymmetricCipher::decrypt with
//    key and nonce
//
// An instance of HybridCipher may be used repeatedly for multiple
// encryptions or decryptions. The method SetPublicKey() must be used before
// encryptions and SetPrivateKey() must be used before decryptions.
//
// TODO(pseudorandom): Can we eliminate salt and save space without
// compromising security?
class HybridCipher {
 public:
  static const size_t PUBLIC_KEY_SIZE    = 33;  // One byte extra
                                                // for X9.62 serialization
  static const size_t PRIVATE_KEY_SIZE   = 256 / 8;
  static const size_t NONCE_SIZE         = 128 / 8;  // Nonce for symm cipher
  static const size_t SALT_SIZE          = 32 / 8;   // Salt for HKDF

  HybridCipher();
  ~HybridCipher();

  // Sets the public key for encryption. This must be invoked
  // at least once before encrypt is called. Using decryption after
  // SetPublicKey is undefined behavior.
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  //
  // |key| must have length |PUBLIC_KEY_SIZE| and be
  // X9.62 serialized
  bool SetPublicKey(const byte key[PUBLIC_KEY_SIZE]);

  // Sets the private key for decryption. This must be invoked
  // at least once before decrypt is called. Using encryption after
  // SetPrivateKey is undefined behavior.
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  //
  // |key| must have length |PRIVATE_KEY_SIZE| and will be
  // interpreted as big-endian big integer.
  bool SetPrivateKey(const byte key[PRIVATE_KEY_SIZE]);

  // Performs ECDH-based hybrid encryption
  //
  // |ptext| The plain text to be encrypted
  //
  // |ptext_len| The number of bytes of plain text
  //
  // The output is represented in four parts which will be written to
  // |public_key_part_out|, |salt_out|, |nonce_out| and |ctext| respectively
  //
  // |public_key_part_out| must point to a buffer of size |PUBLIC_KEY_SIZE|.
  // The X9.62 serialization of g^y will be written there
  //
  // |salt| is a pointer to a vector that will be modified to store a random
  // salt of size |SALT_SIZE|
  //
  // |nonce_out| must point to a buffer of size  SymmetricCipher::NONCE_SIZE.
  // The nonce will be written there
  //
  // |ctext| A pointer to a vector that will be modified to contain
  // the ciphertext under the derived symmetric key
  //
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  bool Encrypt(const byte *ptext, int ptext_len,
               byte public_key_part_out[PUBLIC_KEY_SIZE],
               byte salt_out[SALT_SIZE],
               byte nonce_out[NONCE_SIZE],
               std::vector<byte>* ctext);

  // Performs ECDH-based hybrid decryption.
  //
  // |public_key_part| must have length |PUBLIC_KEY_SIZE| and be an X9.62
  // encoded elliptic curve group element. After invoking
  // HybridCipher::Encrypt() the argument |public_key_part_out| will contain
  // such a value
  //
  // |salt| The salt to be used in the HKDF step in decryption. Must have size
  // |SALT_SIZE|
  //
  // |nonce| must be of length SymmetricCipher::NONCE_SIZE
  //
  // |ctext| The ciphertext to be decrypted.
  //
  // |ctext_len| The number of bytes of ciphertext.
  //
  // |ptext| A pointer to a vector that will be modified to contain the
  // recovered plaintext.
  //
  // Returns true for success or false for failure. Use the functions
  // in errors.h to obtain error information upon failure.
  bool Decrypt(const byte public_key_part[PUBLIC_KEY_SIZE],
               const byte salt[SALT_SIZE],
               const byte nonce[NONCE_SIZE],
               const byte *ctext, int ctext_len,
               std::vector<byte>* ptext);

 private:
  std::unique_ptr<HybridCipherContext> context_;
  std::unique_ptr<SymmetricCipher> symm_cipher_;
};

}  // namespace crypto

}  // namespace cobalt

#endif  // COBALT_UTIL_CRYPTO_UTIL_CIPHER_H_
