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

#include "algorithms/rappor/rappor_test_utils.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace rappor {
namespace testing {

bool IsSet(const std::string& data, int bit_index) {
  uint32_t num_bytes = data.size();
  uint32_t byte_index = bit_index / 8;
  uint32_t bit_in_byte_index = bit_index % 8;
  return data[num_bytes - byte_index - 1] & (1 << bit_in_byte_index);
}

std::string DataToBinaryString(const std::string& data) {
  size_t num_bits = data.size() * 8;
  // Initialize output to a string of all zeroes.
  std::string output(num_bits, '0');
  size_t output_index = 0;
  for (int bit_index = num_bits - 1; bit_index >= 0; bit_index--) {
    if (IsSet(data, bit_index)) {
      output[output_index] = '1';
    }
    output_index++;
  }
  return output;
}

std::string BinaryStringToData(const std::string& binary_string) {
  size_t num_bits = binary_string.size();
  EXPECT_EQ(0, num_bits % 8);
  size_t num_bytes = num_bits / 8;
  std::string result(num_bytes, static_cast<char>(0));
  size_t byte_index = 0;
  uint8_t bit_mask = 1 << 7;
  for (char c : binary_string) {
    if (c == '1') {
      result[byte_index] |= bit_mask;
    }
    bit_mask >>= 1;
    if (bit_mask == 0) {
      byte_index++;
      bit_mask = 1 << 7;
    }
  }
  return result;
}

std::string CategoryName(uint32_t index) {
  char buffer[13];
  snprintf(buffer, sizeof(buffer), "category%04u", index);
  return std::string(buffer, 12);
}


std::string BuildBitPatternString(int num_bits, int index, char index_char,
    char other_char ) {
  return std::string(num_bits - 1 - index, other_char) + index_char +
      std::string(index, other_char);
}


}  // namespace testing
}  // namespace rappor
}  // namespace cobalt
