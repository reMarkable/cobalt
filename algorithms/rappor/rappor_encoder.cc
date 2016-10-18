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

#include "algorithms/encoder/rappor_encoder.h"

namespace cobalt {
namespace rappor {

using encoder::ClientSecret;

namespace {
// Factors out some common validation logic.
bool BasicValidate(float prob_0_becomes_1, float prob_1_stays_1, float prob_rr,
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
    if (!BasicValidate(prob_0_becomes_1_, prob_1_stays_1_, config.prob_rr(),
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
    if (!BasicValidate(prob_0_becomes_1_, prob_1_stays_1_, config.prob_rr(),
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

  bool valid() {
    return valid_;
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
    client_secret_(std::move(client_secret)) {}

BasicRapporEncoder::~BasicRapporEncoder() {}

Status BasicRapporEncoder::Encode(const std::string& value,
    BasicRapporObservation *observation_out) {
  if (!config_->valid()) {
    return kInvalidConfig;
  }

  // TODO(rudominer) Replace this with a real implementation.
  observation_out->set_data(value);
  return kOK;
}

}  // namespace rappor

}  // namespace cobalt

