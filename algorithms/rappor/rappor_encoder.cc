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

#include <map>

#include "util/crypto_util/random.h"

namespace cobalt {
namespace rappor {

using crypto::byte;
using encoder::ClientSecret;

namespace {
// Factors out some common validation logic.
bool CommonValidate(float prob_0_becomes_1, float prob_1_stays_1, float prob_rr,
                   const ClientSecret& client_secret) {
  // TODO(rudominer) Consider adding logging statements here to help
  // debug an invalid config.
  if (!client_secret.valid()) {
    return false;
  }
  if (prob_0_becomes_1 < 0.0 || prob_0_becomes_1 > 1.0) {
    return false;
  }
  if (prob_1_stays_1 < 0.0 || prob_1_stays_1 > 1.0) {
    return false;
  }
  if (prob_0_becomes_1 == prob_1_stays_1) {
    return false;
  }
  if (prob_rr != 0.0) {
    return false;
  }
  return true;
}
}  // namespace

class RapporConfigValidator {
 public:
  // Constructor for String RAPPOR
  RapporConfigValidator(const RapporConfig& config,
                        const ClientSecret& client_secret) :
      prob_0_becomes_1_(config.prob_0_becomes_1()),
      prob_1_stays_1_(config.prob_1_stays_1()),
      num_bits_(config.num_bloom_bits()),
      num_hashes_(config.num_hashes()),
      num_cohorts_(config.num_cohorts()) {
    valid_ = false;
    if (!CommonValidate(prob_0_becomes_1_, prob_1_stays_1_, config.prob_rr(),
                      client_secret)) {
      return;
    }
    if (num_bits_ < 1 || num_bits_ >= 1024) {
      return;
    }
    if (num_hashes_ < 1 ||num_hashes_ >= 8 || num_hashes_ >= num_bits_) {
      return;
    }
    if (num_cohorts_ < 1 || num_cohorts_ >= 1024) {
      return;
    }
    valid_ = true;
  }

  // Constructor for Basic RAPPOR
  explicit RapporConfigValidator(const BasicRapporConfig& config,
                                 const ClientSecret& client_secret) :
      prob_0_becomes_1_(config.prob_0_becomes_1()),
      prob_1_stays_1_(config.prob_1_stays_1()),
      num_bits_(config.category_size()),
      num_hashes_(0),
      num_cohorts_(1) {
    valid_ = false;
    if (!CommonValidate(prob_0_becomes_1_, prob_1_stays_1_, config.prob_rr(),
                      client_secret)) {
      return;
    }
    if (num_bits_ < 1) {
      return;
    }

    // Insert all of the categories into the map.
    size_t index = 0;
    for (auto category : config.category()) {
      if (category.empty()) {
        return;
      }
      auto result = category_to_bit_index_.emplace(category, index++);
      if (!result.second) {
        return;
      }
    }

    valid_ = true;
  }

  float prob_0_becomes_1() {
    return prob_0_becomes_1_;
  }

  float prob_1_stays_1() {
    return prob_1_stays_1_;
  }

  bool valid() {
    return valid_;
  }

  size_t num_bits() {
    return num_bits_;
  }

  // Returns the bit-index of |category| or -1 if |category| is not one of the
  // basic RAPPOR categories (or if this object was not initialized with a
  // BasicRapporConfig.)
  size_t bit_index(const std::string& category) {
    auto iterator = category_to_bit_index_.find(category);
    if (iterator == category_to_bit_index_.end()) {
      return -1;
    }
    return iterator->second;
  }

 private:
  bool valid_;
  float prob_0_becomes_1_;
  float prob_1_stays_1_;
  uint32_t num_bits_;

  // Used only in string RAPPOR
  uint32_t num_hashes_;
  uint32_t num_cohorts_;

  // Used only in Basic RAPPOR
  std::map<std::string, size_t> category_to_bit_index_;
};

RapporEncoder::RapporEncoder(const RapporConfig& config,
                             ClientSecret client_secret) :
    config_(new RapporConfigValidator(config, client_secret)),
    random_(new crypto::Random()),
    client_secret_(std::move(client_secret)) {}

RapporEncoder::~RapporEncoder() {}

Status RapporEncoder::Encode(const std::string& value,
                             RapporObservation *observation_out) {
  if (!config_->valid()) {
    return kInvalidConfig;
  }

  // TODO(rudominer) Replace this with a real implementation.
  observation_out->set_cohort(42);
  observation_out->set_data(value);
  return kOK;
}

BasicRapporEncoder::BasicRapporEncoder(const BasicRapporConfig& config,
                                      ClientSecret client_secret) :
    config_(new RapporConfigValidator(config, client_secret)),
    random_(new crypto::Random()),
    client_secret_(std::move(client_secret)) {}

BasicRapporEncoder::~BasicRapporEncoder() {}

Status BasicRapporEncoder::Encode(const std::string& value,
    BasicRapporObservation *observation_out) {
  if (!config_->valid()) {
    return kInvalidConfig;
  }
  size_t bit_index = config_->bit_index(value);
  if (bit_index == -1) {
    return kInvalidInput;
  }

  uint32_t num_bits = config_->num_bits();
  uint32_t num_bytes = num_bits/8 + (num_bits % 8 == 0 ? 0 : 1);

  // Indexed from the right, i.e. the least-significant bit.
  uint32_t byte_index = bit_index / 8;
  uint32_t bit_in_byte_index = bit_index % 8;

  // Initialize data to a string of all zero bytes.
  std::string data(num_bytes, static_cast<char>(0));

  // Set the appropriate bit.
  data[num_bytes - (byte_index + 1)] = 1 << bit_in_byte_index;

  // TODO(rudominer) We will not support RAPPOR PRR in version 0.1 of Cobalt but
  // consider supporting in in future versions.

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

