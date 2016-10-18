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

namespace cobalt {
namespace forculus {

ForculusDecrypter::ForculusDecrypter(const ForculusConfig& config,
                                     std::string ciphertext) :
  config_(config), ciphertext_(std::move(ciphertext)) {}

ForculusDecrypter::Status ForculusDecrypter::AddPoint(std::string x,
                                                      std::string y) {
  points_.emplace_back(std::move(x), std::move(y));
  return kOK;
}

ForculusDecrypter::Status ForculusDecrypter::Decrypt(
    std::string *plain_text_out) {
  // TODO(rudominer) Replace this dummy implementation with a real one.
  plain_text_out->assign(ciphertext_);
  return kOK;
}

}  // namespace forculus
}  // namespace cobalt

