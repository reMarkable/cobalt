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
// Factors out some common validation logic.
bool CommonValidate(float prob_0_becomes_1, float prob_1_stays_1, float prob_rr,
                   const ClientSecret& client_secret) {
  if (!client_secret.valid()) {
    VLOG(3) << "client_secret is not valid";
    return false;
  }
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

// Extracts the categories from |config|. We support both string and integer
// categories. In order to handle these two cases uniformly, we use as the
// canonical identifier for a category a std::string containing the serialized
// bytes of a |ValuePart| that contains the string or int value of the category.
// There is one exception to this: If a category is the empty string then we
// represent that category as the empty string. This is so that we can easily
// identify the empty string which we want to do because it is invalid as
// a category. Returns an empty vector if |config| does not contain either
// string or int categories.
std::vector<std::string> Categories(const BasicRapporConfig& config) {
  std::vector<std::string> categories;
  switch (config.categories_case()) {
    case BasicRapporConfig::kStringCategories: {
      size_t num_categories = config.string_categories().category_size();
      if (num_categories <=1 || num_categories >= 1024) {
        // Return an empty vector to indicate an error.
        return categories;
      }
      for (auto category : config.string_categories().category()) {
        if (category.empty()) {
          categories.push_back("");
        } else {
          ValuePart value_part;
          value_part.set_string_value(category);
          std::string serialized_value_part;
          value_part.SerializeToString(&serialized_value_part);
          categories.push_back(serialized_value_part);
        }
      }
    } break;
    case BasicRapporConfig::kIntRangeCategories: {
      int64_t first = config.int_range_categories().first();
      int64_t last = config.int_range_categories().last();
      int64_t num_categories = last - first + 1;
      if (last <= first || num_categories >= 1024) {
        // Return an empty vector to indicate an error.
        return categories;
      }
      for (int64_t category = first; category <= last; category++) {
        ValuePart value_part;
        value_part.set_int_value(category);
        std::string serialized_value_part;
        value_part.SerializeToString(&serialized_value_part);
        categories.push_back(serialized_value_part);
      }
    } break;
    default:
      break;
  }
  return categories;
}

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
      num_bits_(0),
      num_hashes_(0),
      num_cohorts_(1) {
    valid_ = false;
    if (!CommonValidate(prob_0_becomes_1_, prob_1_stays_1_, config.prob_rr(),
                      client_secret)) {
      return;
    }
    std::vector<std::string> categories = Categories(config);
    num_bits_ = categories.size();
    if (num_bits_ < 1) {
      return;
    }

    // Insert all of the categories into the map.
    size_t index = 0;
    for (auto category : categories) {
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
  int bit_index(const std::string& category) {
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

Status RapporEncoder::Encode(const ValuePart& value,
                             RapporObservation *observation_out) {
  if (!config_->valid()) {
    return kInvalidConfig;
  }

  // TODO(rudominer) Replace this with a real implementation.
  observation_out->set_cohort(42);
  observation_out->set_data(value.string_value());
  return kOK;
}

BasicRapporEncoder::BasicRapporEncoder(const BasicRapporConfig& config,
                                      ClientSecret client_secret) :
    config_(new RapporConfigValidator(config, client_secret)),
    random_(new crypto::Random()),
    client_secret_(std::move(client_secret)) {}

BasicRapporEncoder::~BasicRapporEncoder() {}

Status BasicRapporEncoder::Encode(const ValuePart& value,
    BasicRapporObservation *observation_out) {
  if (!config_->valid()) {
    return kInvalidConfig;
  }
  std::string serialized_value;
  value.SerializeToString(&serialized_value);
  size_t bit_index = config_->bit_index(serialized_value);
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

