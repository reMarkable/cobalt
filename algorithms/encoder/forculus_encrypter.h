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

#ifndef COBALT_ALGORITHMS_ENCODER_FORCULUS_ENCRYPTER_H_
#define COBALT_ALGORITHMS_ENCODER_FORCULUS_ENCRYPTER_H_

#include <string>
#include <utility>

#include "./cobalt.pb.h"
#include "./encodings.pb.h"

namespace cobalt {
namespace forculus {

// Encrypts a string value using Forculus threshold encryption. This API
// is intended for use in the Cobalt Encoder.
class ForculusEncrypter {
 public:
  // Constructor.
  // The |client_id| is used as a seed when generating random points on
  // the polynomial. Therefore the client_id should be derived using some
  // scheme that uniquely and permanently identifies each logical client.
  ForculusEncrypter(const ForculusConfig& config, std::string client_id) :
      config_(config), client_id_(std::move(client_id)) {}

  // Encrypts |value| using Forculus threshold encryption and returns
  // a |ForculusObservation| containing the ciphertext and a random
  // point on the polynomial.
  //
  // |day_index| is used to determine the current epoch, based on the EpochType
  // as specified in the ForculusConfig passed into the constructor. Forculus
  // encrypion depends on the current epoch.
  ForculusObservation Encrypt(const std::string& value, uint32_t day_index);

 private:
  ForculusConfig config_;
  std::string client_id_;
};

}  // namespace forculus

}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_ENCODER_FORCULUS_ENCRYPTER_H_
