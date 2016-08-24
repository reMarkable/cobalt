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
#include "third_party/boringssl/src/include/openssl/rand.h"

namespace cobalt {

namespace crypto {

namespace {
  const EVP_AEAD* GetAEAD() {
    // Note(rudominer) The constants KEY_SIZE and NONCE_SIZE are set based
    // on the algorithm chosen. If this algorithm changes you must also
    // change those constants accordingly.
    return EVP_aead_aes_256_gcm();
  }
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

// SymmetricCipher methods.

SymmetricCipher::SymmetricCipher() : context_(new CipherContext()) {}

SymmetricCipher::~SymmetricCipher() {}

bool SymmetricCipher::setKey(const byte key[KEY_SIZE]) {
  return EVP_AEAD_CTX_init(context_->get(), GetAEAD(), key,
                           KEY_SIZE, EVP_AEAD_DEFAULT_TAG_LENGTH, NULL);
}

bool SymmetricCipher::encrypt(const byte nonce[NONCE_SIZE], const byte *ptext,
    int ptext_len, std::vector<byte>* ctext) {

  int max_out_len = EVP_AEAD_max_overhead(GetAEAD()) + ptext_len;
  ctext->resize(max_out_len);
  size_t out_len;
  int rc = EVP_AEAD_CTX_seal(context_->get(), ctext->data(), &out_len,
      max_out_len, nonce, NONCE_SIZE, ptext, ptext_len, NULL, 0);
  ctext->resize(out_len);
  return rc;
}

bool SymmetricCipher::decrypt(const byte nonce[NONCE_SIZE], const byte *ctext,
  int ctext_len, std::vector<byte>* rtext) {
  rtext->resize(ctext_len);
  size_t out_len;
  int rc = EVP_AEAD_CTX_open(context_->get(), rtext->data(), &out_len,
      rtext->size(), nonce, NONCE_SIZE, ctext, ctext_len, NULL, 0);
  rtext->resize(out_len);
  return rc;
}

}  // namespace crypto

}  // namespace cobalt

