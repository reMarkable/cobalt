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

#include <cstring>
#include <map>
#include <vector>

#include "util/crypto_util/hash.h"
#include "util/crypto_util/mac.h"
#include "util/crypto_util/random.h"

namespace cobalt {
namespace rappor {

using crypto::byte;
using crypto::hmac::HMAC;
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

// Flips the bits in |data| using the given probabilities and the given RNG.
//
// p = prob_0_becomes_1
// q = prob_1_stays_1
void FlipBits(double p, double q, crypto::Random* random, std::string* data) {
  for (int i = 0; i < data->size(); i++) {
    byte p_mask = random->RandomBits(p);
    byte q_mask = random->RandomBits(q);
    data->at(i) = (p_mask & ~data->at(i)) | (q_mask & data->at(i));
  }
}

}  // namespace

RapporEncoder::RapporEncoder(const RapporConfig& config,
                             ClientSecret client_secret)
    : config_(new RapporConfigValidator(config)),
      random_(new crypto::Random()),
      client_secret_(std::move(client_secret)),
      cohort_num_(DeriveCohortFromSecret()) {}

RapporEncoder::~RapporEncoder() {}

std::string RapporEncoder::MakeBloomBits(const ValuePart& value) {
  uint32_t num_bits = config_->num_bits();
  uint32_t num_bytes = (num_bits + 7) / 8;
  uint32_t num_hashes = config_->num_hashes();

  std::string serialized_value;
  value.SerializeToString(&serialized_value);

  // We append the cohort to the value before hashing.
  std::vector<byte> hash_input(serialized_value.size() + sizeof(cohort_num_));
  std::memcpy(hash_input.data(), &serialized_value[0], serialized_value.size());
  std::memcpy(hash_input.data() + serialized_value.size(), &cohort_num_,
              sizeof(cohort_num_));

  // Now we hash |hash_input| into |hashed_value|.
  // We are going to use two bytes of |hashed_value| for each hash in the Bloom
  // filter so we need DIGEST_SIZE to be at least num_hashes*2. This should have
  // already been checked at config validation time.
  CHECK(crypto::hash::DIGEST_SIZE >= num_hashes * 2);
  byte hashed_value[crypto::hash::DIGEST_SIZE];
  if (!crypto::hash::Hash(hash_input.data(), hash_input.size(), hashed_value)) {
    VLOG(1) << "Hash() failed";
    return "";
  }

  // Initialize data to a string of all zero bytes.
  // (The C++ Protocol Buffer API uses string to represent an array of bytes.)
  std::string data(num_bytes, static_cast<char>(0));
  for (size_t hash_index = 0; hash_index < num_hashes; hash_index++) {
    // Each bloom filter consumes two bytes of |hashed_value|. Note that
    // num_bits is required to be a power of 2 (this is checked in the
    // constructor of RapporConfigValidator) so that the mod operation below
    // preserves the uniform distribution of |hashed_value|.
    uint32_t bit_index =
        (*reinterpret_cast<uint16_t*>(&hashed_value[hash_index * 2])) %
        num_bits;

    // Indexed from the right, i.e. the least-significant bit.
    uint32_t byte_index = bit_index / 8;
    uint32_t bit_in_byte_index = bit_index % 8;
    // Set the appropriate bit.
    data[num_bytes - (byte_index + 1)] |= 1 << bit_in_byte_index;
  }

  return data;
}

// We use HMAC as a PRF and compute
// HMAC_{client_secret}(attempt_number) % num_cohorts_2_power
uint32_t RapporEncoder::AttemptDeriveCohortFromSecret(size_t attempt_number) {
  if (!config_->valid()) {
    VLOG(1) << "config is not valid";
    return UINT32_MAX;
  }
  if (!client_secret_.valid()) {
    VLOG(1) << "client_secret is not valid";
    return UINT32_MAX;
  }

  // Invoke HMAC.
  byte hashed_value[crypto::hmac::TAG_SIZE];
  if (!HMAC(client_secret_.data(), ClientSecret::kNumSecretBytes,
            reinterpret_cast<byte*>(&attempt_number), sizeof(attempt_number),
            hashed_value)) {
    VLOG(1) << "HMAC() failed!";
    return UINT32_MAX;
  }

  // Interpret the first two bytes of hashed_value as an unsigned integer
  // and mod by num_cohorts_2_power.
  CHECK_GT(config_->num_cohorts_2_power(), 0);
  return *(reinterpret_cast<uint16_t*>(hashed_value)) %
         config_->num_cohorts_2_power();
}

uint32_t RapporEncoder::DeriveCohortFromSecret() {
  size_t attempt_number = 0;
  // Each invocation of AttemptDeriveCohortFromSecret() has probability > 1/2
  // of returning a value < num_cohorts so the probability that this loop
  // will execute more than n times is less than 1/(2^n).
  while (true) {
    uint32_t cohort = AttemptDeriveCohortFromSecret(attempt_number++);
    if (cohort == UINT32_MAX) {
      // Derivation failed.
      return UINT32_MAX;
    }
    if (cohort < config_->num_cohorts()) {
      return cohort;
    }
  }
}

Status RapporEncoder::Encode(const ValuePart& value,
                             RapporObservation* observation_out) {
  if (!config_->valid()) {
    return kInvalidConfig;
  }
  if (!client_secret_.valid()) {
    VLOG(3) << "client_secret is not valid";
    return kInvalidConfig;
  }
  if (cohort_num_ == UINT32_MAX) {
    VLOG(1) << "Unable to derive cohort from client_secret.";
    return kInvalidConfig;
  }

  std::string data = MakeBloomBits(value);
  if (data.empty()) {
    VLOG(3) << "MakeBloomBits failed on input: " << DebugString(value);
    return kInvalidInput;
  }

  // TODO(rudominer) Consider supporting prr in future versions of Cobalt.

  // Randomly flip some of the bits based on the probabilities p and q.
  FlipBits(config_->prob_0_becomes_1(), config_->prob_1_stays_1(),
           random_.get(), &data);

  observation_out->set_cohort(cohort_num_);
  observation_out->set_data(data);
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
  FlipBits(config_->prob_0_becomes_1(), config_->prob_1_stays_1(),
           random_.get(), &data);

  observation_out->set_data(data);
  return kOK;
}

}  // namespace rappor

}  // namespace cobalt
