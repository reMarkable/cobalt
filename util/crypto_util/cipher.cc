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

#include <memory>
#include <vector>

#include "third_party/boringssl/src/include/openssl/aead.h"
#include "third_party/boringssl/src/include/openssl/bn.h"
#include "third_party/boringssl/src/include/openssl/digest.h"
#include "third_party/boringssl/src/include/openssl/ec.h"
#include "third_party/boringssl/src/include/openssl/ecdh.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/hkdf.h"
#include "util/crypto_util/errors.h"
#include "util/crypto_util/random.h"

namespace cobalt {

namespace crypto {

namespace {
// Note(pseudorandom) Curve constants are defined in
// third_party/boringssl/src/include/openssl/nid.h. NID_X9_62_prime256v1
// refers to the NIST p256 curve (secp256r1 or P-256 defined in FIPS 186-4
// here http://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.186-4.pdf)
#define EC_CURVE_CONSTANT NID_X9_62_prime256v1

  const EVP_AEAD* GetAEAD() {
    // Note(rudominer) The constants KEY_SIZE and NONCE_SIZE are set based
    // on the algorithm chosen. If this algorithm changes you must also
    // change those constants accordingly.
    //
    // NOTE(pseudorandom) By using a 256-bit curve in EC_CURVE_CONSTANT for
    // public-key cryptography when SymmetricCipher is used in HybridCipher,
    // the effective security level is AES-128 and not AES-256.
    return EVP_aead_aes_256_gcm();
  }
  static const size_t GROUP_ELEMENT_SIZE  = 256 / 8;  // (g^xy) object length
}  //  namespace

class CipherContext {
 public:
  CipherContext()
      : impl_(new EVP_AEAD_CTX(), ::EVP_AEAD_CTX_cleanup) {}

  EVP_AEAD_CTX* get() {
    return impl_.get();
  }

 private:
  std::unique_ptr<EVP_AEAD_CTX, decltype(&::EVP_AEAD_CTX_cleanup)> impl_;
};

class HybridCipherContext {
 public:
  HybridCipherContext()
      : key_(nullptr, ::EVP_PKEY_free) {}

  bool ResetKey() {
    key_.reset(EVP_PKEY_new());
    return (nullptr != key_);
  }

  EVP_PKEY* GetKey() {
    return key_.get();
  }

 private:
  std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)> key_;
};

// SymmetricCipher methods.

SymmetricCipher::SymmetricCipher() : context_(new CipherContext()) {}

SymmetricCipher::~SymmetricCipher() {}

bool SymmetricCipher::set_key(const byte key[KEY_SIZE]) {
  return EVP_AEAD_CTX_init(context_->get(), GetAEAD(), key,
                           KEY_SIZE, EVP_AEAD_DEFAULT_TAG_LENGTH, NULL);
}

bool SymmetricCipher::Encrypt(const byte nonce[NONCE_SIZE], const byte *ptext,
    int ptext_len, std::vector<byte>* ctext) {

  int max_out_len = EVP_AEAD_max_overhead(GetAEAD()) + ptext_len;
  ctext->resize(max_out_len);
  size_t out_len;
  int rc = EVP_AEAD_CTX_seal(context_->get(), ctext->data(), &out_len,
      max_out_len, nonce, NONCE_SIZE, ptext, ptext_len, NULL, 0);
  ctext->resize(out_len);
  return rc;
}

bool SymmetricCipher::Decrypt(const byte nonce[NONCE_SIZE], const byte *ctext,
  int ctext_len, std::vector<byte>* ptext) {
  ptext->resize(ctext_len);
  size_t out_len;
  int rc = EVP_AEAD_CTX_open(context_->get(), ptext->data(), &out_len,
      ptext->size(), nonce, NONCE_SIZE, ctext, ctext_len, NULL, 0);
  ptext->resize(out_len);
  return rc;
}

// HybridCipher methods.

HybridCipher::HybridCipher() : context_(new HybridCipherContext()),
                               symm_cipher_(new SymmetricCipher) {}

HybridCipher::~HybridCipher() {}

bool HybridCipher::set_public_key(const byte public_key[PUBLIC_KEY_SIZE]) {
  std::unique_ptr<EC_KEY, decltype(&::EC_KEY_free)> eckey(
      EC_KEY_new_by_curve_name(EC_CURVE_CONSTANT), EC_KEY_free);
  if (!eckey) {
    return false;
  }

  std::unique_ptr<EC_POINT, decltype(&::EC_POINT_free)> ecpoint(
      EC_POINT_new(EC_KEY_get0_group(eckey.get())), EC_POINT_free);
  if (!ecpoint) {
    return false;
  }

  // Read bytes from public_key into ecpoint
  if (!EC_POINT_oct2point(EC_KEY_get0_group(eckey.get()), ecpoint.get(),
                         public_key, PUBLIC_KEY_SIZE, nullptr)) {
    return false;
  }

  // Setup eckey with public key from ecpoint
  if (!EC_KEY_set_public_key(eckey.get(), ecpoint.get())) {
    return false;
  }

  // Setup pkey with EC public key eckey
  context_->ResetKey();
  if (!EVP_PKEY_set1_EC_KEY(context_->GetKey(), eckey.get())) {
    return false;
  }

  // Success
  return true;
}

bool HybridCipher::set_private_key(const byte private_key[PRIVATE_KEY_SIZE]) {
  std::unique_ptr<EC_KEY, decltype(&::EC_KEY_free)> eckey(
      EC_KEY_new_by_curve_name(EC_CURVE_CONSTANT), EC_KEY_free);
  if (!eckey) {
    return false;
  }

  std::unique_ptr<BIGNUM, decltype(&::BN_free)> bn_private_key(
      BN_bin2bn(private_key, PRIVATE_KEY_SIZE, nullptr), BN_free);
  if (!bn_private_key) {
    return false;
  }

  // Read bytes from public_key into BIGNUM object
  if (!EC_KEY_set_private_key(eckey.get(), bn_private_key.get())) {
    return false;
  }

  // Setup pkey with EC public key eckey
  context_->ResetKey();
  if (!EVP_PKEY_set1_EC_KEY(context_->GetKey(), eckey.get())) {
    return false;
  }

  // Success
  return true;
}

bool HybridCipher::Encrypt(const byte *ptext, int ptext_len,
                           byte public_key_part_out[PUBLIC_KEY_SIZE],
                           byte salt_out[SALT_SIZE],
                           byte nonce_out[NONCE_SIZE],
                           std::vector<byte>* ctext) {
  std::unique_ptr<EC_KEY, decltype(&::EC_KEY_free)> eckey(
      EC_KEY_new_by_curve_name(EC_CURVE_CONSTANT), EC_KEY_free);
  if (!eckey) {
    return false;
  }

  // Generate fresh EC key. The public key will be published in
  // public_key_part and the EC key will be used to generate a symmetric key
  // that encrypts ptext bytes into ctext
  if (!EC_KEY_generate_key(eckey.get())) {
    return false;
  }

  // Write EC public key into public_key_part
  if (EC_POINT_point2oct(EC_KEY_get0_group(eckey.get()),
                         EC_KEY_get0_public_key(eckey.get()),
                         POINT_CONVERSION_COMPRESSED,
                         public_key_part_out,
                         PUBLIC_KEY_SIZE, nullptr) != PUBLIC_KEY_SIZE) {
    return false;
  }

  byte shared_key[GROUP_ELEMENT_SIZE];  // To store g^(xy) after ECDH
  const EC_POINT *ec_pub_point =
      EC_KEY_get0_public_key(EVP_PKEY_get0_EC_KEY(context_->GetKey()));
  size_t shared_key_len = ECDH_compute_key(shared_key, sizeof(shared_key),
                                           ec_pub_point, eckey.get(), nullptr);
  if (shared_key_len != sizeof(shared_key)) {
    return false;
  }

  // Fill salt with random bytes
  Random rand;
  rand.RandomBytes(salt_out, SALT_SIZE);

  // Derive hkdf_derived_key by running HKDF with SHA256 and random salt
  byte hkdf_derived_key[SymmetricCipher::KEY_SIZE];
  if (!HKDF(hkdf_derived_key, SymmetricCipher::KEY_SIZE, EVP_sha256(),
           shared_key, shared_key_len,
           salt_out, SALT_SIZE,
           nullptr, 0)) {
    return false;
  }

  // Choose random nonce for symmetric encryption
  // TODO(pseudorandom): Verify crypto math and then set nonce to all zeroes
  // whenever using AES-GCM in hybrid mode
  rand.RandomBytes(nonce_out, NONCE_SIZE);

  // Do symmetric encryption with hkdf_derived_key
  if (!symm_cipher_->set_key(hkdf_derived_key)) {
    return false;
  }
  if (!symm_cipher_->Encrypt(nonce_out, ptext, ptext_len, ctext)) {
    return false;
  }

  // Success
  return true;
}

bool HybridCipher::Decrypt(const byte public_key_part[PUBLIC_KEY_SIZE],
                           const byte salt[SALT_SIZE],
                           const byte nonce[NONCE_SIZE],
                           const byte *ctext, int ctext_len,
                           std::vector<byte>* ptext) {
  // Read public_key_part into new EVP_PKEY object
  std::unique_ptr<EC_KEY, decltype(&::EC_KEY_free)> eckey(
      EC_KEY_new_by_curve_name(EC_CURVE_CONSTANT), EC_KEY_free);
  if (!eckey) {
    return false;
  }
  std::unique_ptr<EC_POINT, decltype(&::EC_POINT_free)> ecpoint(
      EC_POINT_new(EC_KEY_get0_group(eckey.get())), EC_POINT_free);
  if (!ecpoint) {
    return false;
  }

  // Read bytes from public_key_part into ecpoint
  if (!EC_POINT_oct2point(EC_KEY_get0_group(eckey.get()),
                          ecpoint.get(), public_key_part,
                          PUBLIC_KEY_SIZE, nullptr)) {
    return false;
  }

  // Setup eckey with public key from ecpoint
  if (!EC_KEY_set_public_key(eckey.get(), ecpoint.get())) {
    return false;
  }

  byte shared_key[GROUP_ELEMENT_SIZE];  // To store g^(xy) after ECDH
  size_t shared_key_len = ECDH_compute_key(shared_key, sizeof(shared_key),
                                ecpoint.get(),
                                EVP_PKEY_get0_EC_KEY(context_->GetKey()),
                                nullptr);

  // Derive hkdf_derived_key by running HKDF with SHA256 and given salt
  byte hkdf_derived_key[SymmetricCipher::KEY_SIZE];
  if (!HKDF(hkdf_derived_key, SymmetricCipher::KEY_SIZE, EVP_sha256(),
           shared_key, shared_key_len,
           salt, SALT_SIZE,
           nullptr, 0)) {
    return false;
  }

  // Now decrypt using symm_cipher_ interface
  if (!symm_cipher_->set_key(hkdf_derived_key)) {
    return false;
  }
  if (!symm_cipher_->Decrypt(nonce, ctext, ctext_len, ptext)) {
    return false;
  }

  // Success
  return true;
}

}  // namespace crypto

}  // namespace cobalt
