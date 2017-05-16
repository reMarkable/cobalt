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

#include "encoder/envelope_maker.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <utility>

#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/project_context.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/encrypted_message_util.h"

namespace cobalt {
namespace encoder {

using config::EncodingRegistry;
using config::MetricRegistry;

namespace {

static const uint32_t kCustomerId = 1;
static const uint32_t kProjectId = 1;
static const char* kAnalyzerPublicKey = "analyzer-public-key";
static const char* kShufflerPublicKey = "shuffler-public-key";

// This unix timestamp corresponds to Friday Dec 2, 2016 in UTC
// and Thursday Dec 1, 2016 in Pacific time.
const time_t kSomeTimestamp = 1480647356;
// This is the day index for Friday Dec 2, 2016
const uint32_t kUtcDayIndex = 17137;

const char* kMetricConfigText = R"(
# Metric 1 has one string part.
element {
  customer_id: 1
  project_id: 1
  id: 1
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
    }
  }
}

# Metric 2 has one string part.
element {
  customer_id: 1
  project_id: 1
  id: 2
  time_zone_policy: UTC
  parts {
    key: "Part1"
    value {
    }
  }
}
)";

const char* kEncodingConfigText = R"(
# EncodingConfig 1 is Forculus.
element {
  customer_id: 1
  project_id: 1
  id: 1
  forculus {
    threshold: 20
  }
}

# EncodingConfig 2 is Basic RAPPOR with string categories.
element {
  customer_id: 1
  project_id: 1
  id: 2
  basic_rappor {
    prob_0_becomes_1: 0.25
    prob_1_stays_1: 0.75
    string_categories: {
      category: "Apple"
      category: "Banana"
      category: "Cantaloupe"
    }
  }
}

)";

// Returns a ProjectContext obtained by parsing the above configuration
// text strings.
std::shared_ptr<ProjectContext> GetTestProject() {
  // Parse the metric config string
  auto metric_parse_result =
      MetricRegistry::FromString(kMetricConfigText, nullptr);
  EXPECT_EQ(config::kOK, metric_parse_result.second);
  std::shared_ptr<MetricRegistry> metric_registry(
      metric_parse_result.first.release());

  // Parse the encoding config string
  auto encoding_parse_result =
      EncodingRegistry::FromString(kEncodingConfigText, nullptr);
  EXPECT_EQ(config::kOK, encoding_parse_result.second);
  std::shared_ptr<EncodingRegistry> encoding_registry(
      encoding_parse_result.first.release());

  return std::shared_ptr<ProjectContext>(new ProjectContext(
      kCustomerId, kProjectId, metric_registry, encoding_registry));
}

}  // namespace

class EnvelopeMakerTest : public ::testing::Test {
 public:
  EnvelopeMakerTest()
      : envelope_maker_(kAnalyzerPublicKey, EncryptedMessage::NONE,
                        kShufflerPublicKey, EncryptedMessage::NONE),
        project_(GetTestProject()),
        encoder_(project_, ClientSecret::GenerateNewSecret()) {
    // Set a static current time so we can test the day_index computation.
    encoder_.set_current_time(kSomeTimestamp);
  }

  // The metric is expected to have a single string part named "Part1" and
  // to use the UTC timezone.
  void AddStringObservation(std::string value, uint32_t metric_id,
                            uint32_t encoding_config_id,
                            size_t expected_num_batches,
                            size_t expected_this_batch_index,
                            size_t expected_this_batch_size) {
    // Encode an Observation
    Encoder::Result result =
        encoder_.EncodeString(metric_id, encoding_config_id, value);
    ASSERT_EQ(Encoder::kOK, result.status);
    ASSERT_NE(nullptr, result.observation);
    ASSERT_NE(nullptr, result.metadata);

    // Add the Observation to the EnvelopeMaker
    envelope_maker_.AddObservation(*result.observation,
                                   std::move(result.metadata));

    // Check the number of batches currently in the envelope.
    ASSERT_EQ(expected_num_batches, envelope_maker_.envelope().batch_size());

    // Check the size of the expected batch.
    const auto& batch =
        envelope_maker_.envelope().batch(expected_this_batch_index);
    ASSERT_EQ(expected_this_batch_size, batch.encrypted_observation_size());

    // Check the ObservationMetadata of the expected batch.
    const auto& metadata = batch.meta_data();
    EXPECT_EQ(kCustomerId, metadata.customer_id());
    EXPECT_EQ(kProjectId, metadata.project_id());
    EXPECT_EQ(metric_id, metadata.metric_id());
    EXPECT_EQ(kUtcDayIndex, metadata.day_index());

    // Deserialize the most recently added observation from the
    // expected batch.
    EXPECT_EQ(
        EncryptedMessage::NONE,
        batch.encrypted_observation(expected_this_batch_size - 1).scheme());
    std::string serialized_observation =
        batch.encrypted_observation(expected_this_batch_size - 1).ciphertext();
    Observation recovered_observation;
    ASSERT_TRUE(recovered_observation.ParseFromString(serialized_observation));
    // Check that it looks right.
    ASSERT_EQ(1, recovered_observation.parts().size());
    auto iter = recovered_observation.parts().find("Part1");
    ASSERT_TRUE(iter != recovered_observation.parts().cend());
    const auto& part = iter->second;
    ASSERT_EQ(encoding_config_id, part.encoding_config_id());
  }

  // Adds multiple encoded Observations to two different metrics. Test that
  // the EnvelopeMaker behaves correctly.
  void DoTest() {
    // Add two observations for metric 1
    size_t expected_num_batches = 1;
    size_t expected_this_batch_index = 0;
    size_t expected_this_batch_size = 1;
    AddStringObservation("a value", 1, 1, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size);
    expected_this_batch_size = 2;
    AddStringObservation("Apple", 1, 2, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size);

    // Add two observations for metric 2
    expected_num_batches = 2;
    expected_this_batch_index = 1;
    expected_this_batch_size = 1;
    AddStringObservation("a value", 2, 1, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size);
    expected_this_batch_size = 2;
    AddStringObservation("Banana", 2, 2, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size);

    // Add two more observations for metric 1
    expected_this_batch_index = 0;
    expected_this_batch_size = 3;
    AddStringObservation("a value", 1, 1, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size);
    expected_this_batch_size = 4;
    AddStringObservation("Banana", 1, 2, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size);

    // Add two more observations for metric 2
    expected_this_batch_index = 1;
    expected_this_batch_size = 3;
    AddStringObservation("a value", 2, 1, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size);
    expected_this_batch_size = 4;
    AddStringObservation("Cantaloupe", 2, 2, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size);

    // Make the encrypted Envelope.
    EncryptedMessage encrypted_message;
    EXPECT_TRUE(envelope_maker_.MakeEncryptedEnvelope(&encrypted_message));

    // Decrypt encrypted_message. (No actual decryption is involved since
    // we used the NONE encryption scheme.)
    util::MessageDecrypter decrypter("");
    Envelope recovered_envelope;
    EXPECT_TRUE(
        decrypter.DecryptMessage(encrypted_message, &recovered_envelope));

    // Check that it looks right.
    EXPECT_EQ(2, recovered_envelope.batch_size());
    for (size_t i = 0; i < 2; i++) {
      EXPECT_EQ(i + 1, recovered_envelope.batch(i).meta_data().metric_id());
      EXPECT_EQ(4, recovered_envelope.batch(i).encrypted_observation_size());
    }
  }

 protected:
  EnvelopeMaker envelope_maker_;
  std::shared_ptr<ProjectContext> project_;
  Encoder encoder_;
};

// We perform DoTest() three times with a Clear() between each turn.
// This last tests that Clear() works correctly.
TEST_F(EnvelopeMakerTest, TestAll) {
  for (int i = 0; i < 3; i++) {
    DoTest();
    envelope_maker_.Clear();
  }
}

}  // namespace encoder
}  // namespace cobalt
