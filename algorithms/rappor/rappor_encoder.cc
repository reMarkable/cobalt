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

#include "algorithms/rappor/rappor_encoder.h"

#include <glog/logging.h>
#include <map>
#include <vector>

#include "util/crypto_util/random.h"

namespace cobalt {
namespace rappor {

using crypto::byte;
using encoder::ClientSecret;

namespace {

// Returns a human-readable string representation of |value| appropriate
// for debug messages.
std::string DebugString(const ValuePart& value) {
  std::ostringstream stream;
  switch (value.data_case()) {
    case ValuePart::kStringValue:
      stream << "'" << value.string_value() << "'";
      break;
    case ValuePart::kIntValue:
      stream << value.int_value();
      break;
    default:
      stream << "unexpected value type";
  }
  return stream.str();
}

}  // namespace

RapporEncoder::RapporEncoder(const RapporConfig& config,
                             ClientSecret client_secret)
    : config_(new RapporConfigValidator(config)),
      random_(new crypto::Random()),
      client_secret_(std::move(client_secret)) {}

RapporEncoder::~RapporEncoder() {}

Status RapporEncoder::Encode(const ValuePart& value,
                             RapporObservation* observation_out) {
  if (!config_->valid()) {
    return kInvalidConfig;
  }
  if (!client_secret_.valid()) {
    VLOG(3) << "client_secret is not valid";
    return kInvalidConfig;
  }

  // TODO(rudominer) Replace this with a real implementation.
  observation_out->set_cohort(42);
  observation_out->set_data(value.string_value());
  return kOK;
}

BasicRapporEncoder::BasicRapporEncoder(const BasicRapporConfig& config,
                                       ClientSecret client_secret)
    : config_(new RapporConfigValidator(config)),
      random_(new crypto::Random()),
      client_secret_(std::move(client_secret)) {}

BasicRapporEncoder::~BasicRapporEncoder() {}

Status BasicRapporEncoder::Encode(const ValuePart& value,
                                  BasicRapporObservation* observation_out) {
  if (!config_->valid()) {
    return kInvalidConfig;
  }
  if (!client_secret_.valid()) {
    VLOG(3) << "client_secret is not valid";
    return kInvalidConfig;
  }
  size_t bit_index = config_->bit_index(value);
  if (bit_index == -1) {
    VLOG(3) << "BasicRapporEncoder::Encode(): The given value was not one of "
            << "the categories: " << DebugString(value);
    return kInvalidInput;
  }

  uint32_t num_bits = config_->num_bits();
  uint32_t num_bytes = (num_bits + 7) / 8;

  // Indexed from the right, i.e. the least-significant bit.
  uint32_t byte_index = bit_index / 8;
  uint32_t bit_in_byte_index = bit_index % 8;

  // Initialize data to a string of all zero bytes.
  // (The C++ Protocol Buffer API uses string to represent an array of bytes.)
  std::string data(num_bytes, static_cast<char>(0));

  // Set the appropriate bit.
  data[num_bytes - (byte_index + 1)] = 1 << bit_in_byte_index;

  // TODO(rudominer) Consider supporting prr in future versions of Cobalt.

  // Randomly flip some of the bits based on the probabilities p and q.
  double p = config_->prob_0_becomes_1();
  double q = config_->prob_1_stays_1();
  for (int i = 0; i < num_bytes; i++) {
    byte p_mask = random_->RandomBits(p);
    byte q_mask = random_->RandomBits(q);
    data[i] = (p_mask & ~data[i]) | (q_mask & data[i]);
  }
  observation_out->set_data(data);
  return kOK;
}

}  // namespace rappor

}  // namespace cobalt
