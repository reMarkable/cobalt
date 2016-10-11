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

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace rappor {

// Tests the basic functionality of the RapporEncoder.
TEST(RapporEncoderTest, StringRappor) {
  // TODO(rudominer) Replace this dummy test with a real one.
  RapporConfig config;
  RapporEncoder encoder(config, "dummy user ID");
  RapporObservation obs;
  EXPECT_EQ(kOK, encoder.Encode("hello", &obs));
  EXPECT_EQ(42, obs.cohort());
  EXPECT_EQ("hello", obs.data());
}

// Tests the basic functionality of the BasicRapporEncoder.
TEST(RapporEncoderTest, BasicRappor) {
  // TODO(rudominer) Replace this dummy test with a real one.
  BasicRapporConfig config;
  BasicRapporEncoder encoder(config);
  BasicRapporObservation obs;
  EXPECT_EQ(kOK, encoder.Encode("hello", &obs));
  EXPECT_EQ("hello", obs.data());
}

}  // namespace rappor

}  // namespace cobalt

