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

#ifndef COBALT_UTIL_CRYPTO_UTIL_BASE64_H_
#define COBALT_UTIL_CRYPTO_UTIL_BASE64_H_

#include <string>
#include <vector>

#include "util/crypto_util/types.h"

namespace cobalt {
namespace crypto {

// Base64 encodes the bytes in |data| and writes the result into |encoded_out|.
//
// Returns true on success and false on failure.
bool Base64Encode(const std::vector<byte>& data, std::string* encoded_out);

// Base64 decodes |encoded_in| and writes the results into decoded_out.
//
// Returns true on success and false if |encoded_in| could not be decoded.
bool Base64Decode(const std::string& encoded_in,
                  std::vector<byte>* decoded_out);

}  // namespace crypto
}  // namespace cobalt

#endif  // COBALT_UTIL_CRYPTO_UTIL_BASE64_H_
