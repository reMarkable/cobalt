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

#include <cmath>

#include "util/crypto_util/random.h"
#include "third_party/boringssl/src/include/openssl/rand.h"

namespace cobalt {
namespace crypto {

void Random::RandomBytes(byte *buf, std::size_t num) {
  RAND_bytes(buf, num);
}

uint32_t Random::RandomUint32() {
  uint32_t x;
  RandomBytes(reinterpret_cast<byte*>(&x), 4);
  return x;
}

byte Random::RandomBits(float p) {
  if (p <= 0.0 || p > 1.0) {
    return 0;
  }
  byte ret_val = 0;

  // threshold is the integer n in the range [0, 2^32] such that
  // n/2^32 best approximates p.
  uint64_t threshold = round(
      static_cast<double>(p) * (static_cast<double>(UINT32_MAX) + 1));

  for (int i = 0; i < 8; i++) {
    uint8_t random_bit = (RandomUint32() < threshold);
    ret_val |= random_bit << i;
  }
  return ret_val;
}

}  // namespace crypto

}  // namespace cobalt
