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
#include <memory>
#include <utility>

#include "./cobalt.pb.h"
#include "./encodings.pb.h"
#include "encoder/client_secret.h"
#include "util/datetime_util.h"

namespace cobalt {
namespace forculus {

class ForculusConfigValidator;

// Encrypts a string value using Forculus threshold encryption. This API
// is intended for use in the Cobalt Encoder.
class ForculusEncrypter {
 public:
  enum Status {
    kOK = 0,
    kInvalidConfig,
    kInvalidInput,
  };

  // Constructor.
  // The |client_secret| is used as a seed when generating random points on
  // the polynomial.
  ForculusEncrypter(const ForculusConfig& config,
                    encoder::ClientSecret client_secret);

  ~ForculusEncrypter();

  // Encrypts |value| using Forculus threshold encryption and writes the
  // ciphertext and a random point on the polynomial into |*observation_out|.
  //
  // |observation_date| is used to determine the observation epoch, based on
  // the EpochType as specified in the ForculusConfig passed into the
  // constructor. Forculus encrypion uses the observation epoch in conjunction
  // with the plaintext |value| in deriving the coefficients of the Forculus
  // polynomial, including the ciphertext.
  //
  // Returns kOk on success, kInvalidConfig if the |config| passed to the
  // constructor is not valid, or kInvalidInput if observation_date does not
  // represent a valid date (as specified in datetime_util.h).
  Status Encrypt(const std::string& value,
                 const util::CalendarDate& observation_date,
                 ForculusObservation *observation_out);

 private:
  std::unique_ptr<ForculusConfigValidator> config_;
  encoder::ClientSecret client_secret_;
};

}  // namespace forculus
}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_ENCODER_FORCULUS_ENCRYPTER_H_
