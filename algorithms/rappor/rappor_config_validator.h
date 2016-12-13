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

#ifndef COBALT_ALGORITHMS_RAPPOR_RAPPOR_CONFIG_VALIDATOR_H_
#define COBALT_ALGORITHMS_RAPPOR_RAPPOR_CONFIG_VALIDATOR_H_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "./observation.pb.h"
#include "config/encodings.pb.h"

namespace cobalt {
namespace rappor {

class RapporConfigValidator {
 public:
  // Constructor for String RAPPOR
  explicit RapporConfigValidator(const RapporConfig& config);

  // Constructor for Basic RAPPOR
  explicit RapporConfigValidator(const BasicRapporConfig& config);

  ~RapporConfigValidator();

  float prob_0_becomes_1();

  float prob_1_stays_1();

  bool valid();

  uint32_t num_bits();

  // Returns the bit-index of |category| or -1 if |category| is not one of the
  // basic RAPPOR categories (or if this object was not initialized with a
  // BasicRapporConfig.)
  int bit_index(const ValuePart& category);

  // Gives access to the vector of Categories if this object was initialized
  // with a BasicRapporConfig.
  std::vector<ValuePart>& categories();

 private:
  bool valid_;
  float prob_0_becomes_1_;
  float prob_1_stays_1_;
  uint32_t num_bits_;

  // Used only in string RAPPOR
  uint32_t num_hashes_;
  uint32_t num_cohorts_;

  // Used only in Basic RAPPOR. |categories_| is the list of all
  // categories. The keys to |category_to_bit_index_| are serialized
  // ValueParts.
  std::map<std::string, size_t> category_to_bit_index_;
  std::vector<ValuePart> categories_;
};

}  // namespace rappor
}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_RAPPOR_RAPPOR_CONFIG_VALIDATOR_H_
