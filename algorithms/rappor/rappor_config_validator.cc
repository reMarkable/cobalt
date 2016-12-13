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

#include "algorithms/rappor/rappor_config_validator.h"

#include <glog/logging.h>
#include <string>
#include <vector>

#include "./observation.pb.h"

namespace cobalt {
namespace rappor {

namespace {
// Factors out some common validation logic.
bool CommonValidate(float prob_0_becomes_1, float prob_1_stays_1,
                    float prob_rr) {
  if (prob_0_becomes_1 < 0.0 || prob_0_becomes_1 > 1.0) {
    VLOG(3) << "prob_0_becomes_1 is not valid";
    return false;
  }
  if (prob_1_stays_1 < 0.0 || prob_1_stays_1 > 1.0) {
    VLOG(3) << "prob_1_stays_1 < 0.0  is not valid";
    return false;
  }
  if (prob_0_becomes_1 == prob_1_stays_1) {
    VLOG(3) << "prob_0_becomes_1 == prob_1_stays_1";
    return false;
  }
  if (prob_rr != 0.0) {
    VLOG(3) << "prob_rr not supported";
    return false;
  }
  return true;
}

// Extracts the categories from |config| and populates |*categories|.  We
// support string and integer categories and we use ValueParts to represent
// these two uniformly. Returns true if |config| is valid or  false otherwise.
bool ExtractCategories(const BasicRapporConfig& config,
                       std::vector<ValuePart>* categories) {
  switch (config.categories_case()) {
    case BasicRapporConfig::kStringCategories: {
      size_t num_categories = config.string_categories().category_size();
      if (num_categories <= 1 || num_categories >= 1024) {
        return false;
      }
      for (auto category : config.string_categories().category()) {
        if (category.empty()) {
          return false;
        } else {
          ValuePart value_part;
          value_part.set_string_value(category);
          categories->push_back(value_part);
        }
      }
    } break;
    case BasicRapporConfig::kIntRangeCategories: {
      int64_t first = config.int_range_categories().first();
      int64_t last = config.int_range_categories().last();
      int64_t num_categories = last - first + 1;
      if (last <= first || num_categories >= 1024) {
        return false;
      }
      for (int64_t category = first; category <= last; category++) {
        ValuePart value_part;
        value_part.set_int_value(category);
        categories->push_back(value_part);
      }
    } break;
    default:
      return false;
  }
  return true;
}

}  // namespace

RapporConfigValidator::RapporConfigValidator(const RapporConfig& config)
    : prob_0_becomes_1_(config.prob_0_becomes_1()),
      prob_1_stays_1_(config.prob_1_stays_1()),
      num_bits_(config.num_bloom_bits()),
      num_hashes_(config.num_hashes()),
      num_cohorts_(config.num_cohorts()) {
  valid_ = false;
  if (!CommonValidate(prob_0_becomes_1_, prob_1_stays_1_, config.prob_rr())) {
    return;
  }
  if (num_bits_ < 1 || num_bits_ >= 1024) {
    return;
  }
  if (num_hashes_ < 1 || num_hashes_ >= 8 || num_hashes_ >= num_bits_) {
    return;
  }
  if (num_cohorts_ < 1 || num_cohorts_ >= 1024) {
    return;
  }
  valid_ = true;
}

// Constructor for Basic RAPPOR
RapporConfigValidator::RapporConfigValidator(const BasicRapporConfig& config)
    : prob_0_becomes_1_(config.prob_0_becomes_1()),
      prob_1_stays_1_(config.prob_1_stays_1()),
      num_bits_(0),
      num_hashes_(0),
      num_cohorts_(1) {
  valid_ = false;
  if (!CommonValidate(prob_0_becomes_1_, prob_1_stays_1_, config.prob_rr())) {
    return;
  }
  if (!ExtractCategories(config, &categories_)) {
    return;
  }
  num_bits_ = categories_.size();

  // Insert all of the categories into the map.
  size_t index = 0;
  for (const auto& category : categories_) {
    std::string serialized_value_part;
    category.SerializeToString(&serialized_value_part);
    auto result =
        category_to_bit_index_.emplace(serialized_value_part, index++);
    if (!result.second) {
      return;
    }
  }

  valid_ = true;
}

RapporConfigValidator::~RapporConfigValidator() {}

float RapporConfigValidator::prob_0_becomes_1() { return prob_0_becomes_1_; }

float RapporConfigValidator::prob_1_stays_1() { return prob_1_stays_1_; }

bool RapporConfigValidator::valid() { return valid_; }

uint32_t RapporConfigValidator::num_bits() { return num_bits_; }

// Returns the bit-index of |category| or -1 if |category| is not one of the
// basic RAPPOR categories (or if this object was not initialized with a
// BasicRapporConfig.)
int RapporConfigValidator::bit_index(const ValuePart& category) {
  std::string serialized_value;
  category.SerializeToString(&serialized_value);
  auto iterator = category_to_bit_index_.find(serialized_value);
  if (iterator == category_to_bit_index_.end()) {
    return -1;
  }
  return iterator->second;
}

std::vector<ValuePart>& RapporConfigValidator::categories() {
  return categories_;
}

}  // namespace rappor
}  // namespace cobalt
