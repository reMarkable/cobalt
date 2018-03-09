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

#include <utility>

#include "./gtest.h"
#include "./logging.h"
#include "config/config_text_parser.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/project_context.h"
#include "encoder/system_data.h"
#include "third_party/gflags/include/gflags/gflags.h"
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
const size_t kNoOpEncodingByteOverhead = 30;

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

# Metric 3 has one string part.
element {
  customer_id: 1
  project_id: 1
  id: 3
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

# EncodingConfig 3 is NoOp.
element {
  customer_id: 1
  project_id: 1
  id: 3
  no_op_encoding {
  }
}

)";

// Returns a ProjectContext obtained by parsing the above configuration
// text strings.
std::shared_ptr<ProjectContext> GetTestProject() {
  // Parse the metric config string
  auto metric_parse_result =
      config::FromString<RegisteredMetrics>(kMetricConfigText, nullptr);
  EXPECT_EQ(config::kOK, metric_parse_result.second);
  std::shared_ptr<MetricRegistry> metric_registry(
      metric_parse_result.first.release());

  // Parse the encoding config string
  auto encoding_parse_result =
      config::FromString<RegisteredEncodings>(kEncodingConfigText, nullptr);
  EXPECT_EQ(config::kOK, encoding_parse_result.second);
  std::shared_ptr<EncodingRegistry> encoding_registry(
      encoding_parse_result.first.release());

  return std::shared_ptr<ProjectContext>(new ProjectContext(
      kCustomerId, kProjectId, metric_registry, encoding_registry));
}

class FakeSystemData : public SystemDataInterface {
 public:
  FakeSystemData() {
    system_profile_.set_os(SystemProfile::FUCHSIA);
    system_profile_.set_arch(SystemProfile::ARM_64);
    system_profile_.set_board_name("Fake Board Name");
  }

  const SystemProfile& system_profile() const override {
    return system_profile_;
  };

  static void CheckSystemProfile(const Envelope& envelope) {
    // SystemProfile is not placed in the envelope at this time.
    EXPECT_EQ(SystemProfile::UNKNOWN_OS, envelope.system_profile().os());
    EXPECT_EQ(SystemProfile::UNKNOWN_ARCH, envelope.system_profile().arch());
    EXPECT_EQ("", envelope.system_profile().board_name());
  }

 private:
  SystemProfile system_profile_;
};

}  // namespace

class EnvelopeMakerTest : public ::testing::Test {
 public:
  EnvelopeMakerTest()
      : envelope_maker_(
            new EnvelopeMaker(kAnalyzerPublicKey, EncryptedMessage::NONE,
                              kShufflerPublicKey, EncryptedMessage::NONE)),
        project_(GetTestProject()),
        encoder_(project_, ClientSecret::GenerateNewSecret(),
                 &fake_system_data_) {
    // Set a static current time so we can test the day_index computation.
    encoder_.set_current_time(kSomeTimestamp);
  }

  // Returns the current value of envelope_maker_ and resets envelope_maker_
  // to a new EnvelopeMaker constructed using the given optional arguments.
  std::unique_ptr<EnvelopeMaker> ResetEnvelopeMaker(
      size_t max_bytes_each_observation = SIZE_MAX,
      size_t max_num_bytes = SIZE_MAX) {
    std::unique_ptr<EnvelopeMaker> return_val = std::move(envelope_maker_);
    envelope_maker_.reset(new EnvelopeMaker(
        kAnalyzerPublicKey, EncryptedMessage::NONE, kShufflerPublicKey,
        EncryptedMessage::NONE, max_bytes_each_observation, max_num_bytes));
    return return_val;
  }

  // The metric is expected to have a single string part named "Part1" and
  // to use the UTC timezone.
  // expected_size_change: What is the expected change in the size of the
  // envelope in bytes due to the AddObservation()?
  void AddStringObservation(std::string value, uint32_t metric_id,
                            uint32_t encoding_config_id,
                            int expected_num_batches,
                            size_t expected_this_batch_index,
                            int expected_this_batch_size,
                            size_t expected_size_change,
                            EnvelopeMaker::AddStatus expected_status) {
    // Encode an Observation
    Encoder::Result result =
        encoder_.EncodeString(metric_id, encoding_config_id, value);
    ASSERT_EQ(Encoder::kOK, result.status);
    ASSERT_NE(nullptr, result.observation);
    ASSERT_NE(nullptr, result.metadata);

    // Add the Observation to the EnvelopeMaker
    size_t size_before_add = envelope_maker_->size();
    ASSERT_EQ(expected_status,
              envelope_maker_->AddObservation(*result.observation,
                                              std::move(result.metadata)));
    size_t size_after_add = envelope_maker_->size();
    EXPECT_EQ(expected_size_change, size_after_add - size_before_add) << value;

    // Check the number of batches currently in the envelope.
    ASSERT_EQ(expected_num_batches, envelope_maker_->envelope().batch_size());

    if (expected_status != EnvelopeMaker::kOk) {
      return;
    }

    // Check the ObservationMetadata of the expected batch.
    const auto& batch =
        envelope_maker_->envelope().batch(expected_this_batch_index);
    const auto& metadata = batch.meta_data();
    EXPECT_EQ(kCustomerId, metadata.customer_id());
    EXPECT_EQ(kProjectId, metadata.project_id());
    EXPECT_EQ(metric_id, metadata.metric_id());
    EXPECT_EQ(kUtcDayIndex, metadata.day_index());

    // Check the size of the expected batch.
    ASSERT_EQ(expected_this_batch_size, batch.encrypted_observation_size())
        << "batch_index=" << expected_this_batch_index
        << "; metric_id=" << metric_id;

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
    ASSERT_EQ(1u, recovered_observation.parts().size());
    auto iter = recovered_observation.parts().find("Part1");
    ASSERT_TRUE(iter != recovered_observation.parts().cend());
    const auto& part = iter->second;
    ASSERT_EQ(encoding_config_id, part.encoding_config_id());
  }

  // Adds multiple string observations to the EnvelopeMaker for the given
  // metric_id and for encoding_config_id=3, the NoOp encoding. The string
  // values will be "value<i>" for i in [first, limit).
  // expected_num_batches: How many batches do we expecte the EnvelopeMaker to
  // contain after the first add.
  // expected_this_batch_index: Which batch index do we expect this add to
  // have gone into.
  // expected_this_batch_size: What is the expected size of the current batch
  // *before* the first add.
  void AddManyStringsNoOp(int first, int limit, uint32_t metric_id,
                          int expected_num_batches,
                          size_t expected_this_batch_index,
                          int expected_this_batch_size) {
    static const uint32_t kEncodingConfigId = 3;
    for (int i = first; i < limit; i++) {
      std::ostringstream stream;
      stream << "value " << i;
      size_t expected_observation_num_bytes =
          kNoOpEncodingByteOverhead + (i >= 10 ? 8 : 7);
      expected_this_batch_size++;
      AddStringObservation(stream.str(), metric_id, kEncodingConfigId,
                           expected_num_batches, expected_this_batch_index,
                           expected_this_batch_size,
                           expected_observation_num_bytes, EnvelopeMaker::kOk);
    }
  }

  // Adds multiple encoded Observations to two different metrics. Test that
  // the EnvelopeMaker behaves correctly.
  void DoTest() {
    // Add two observations for metric 1
    size_t expected_num_batches = 1;
    size_t expected_this_batch_index = 0;
    size_t expected_this_batch_size = 1;
    // NOTE(rudominer) The values of expected_observation_num_bytes for
    // the Forculus and Basic RAPPOR encodings in this test are obtained from
    // experimentation rather than calculation. We are therefore not testing
    // that the values are correct but rather testing that there is no
    // regression in the size() functionality. Also just eybealling the numbers
    // serves as a sanity test. Notice that the Forculus Observations are
    // rather large compared to the Basic RAPPOR observations with 3 categories.
    size_t expected_observation_num_bytes = 121;
    AddStringObservation("a value", 1, 1, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size,
                         expected_observation_num_bytes, EnvelopeMaker::kOk);
    expected_this_batch_size = 2;
    expected_observation_num_bytes = 29;
    AddStringObservation("Apple", 1, 2, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size,
                         expected_observation_num_bytes, EnvelopeMaker::kOk);

    // Add two observations for metric 2
    expected_num_batches = 2;
    expected_this_batch_index = 1;
    expected_this_batch_size = 1;
    expected_observation_num_bytes = 122;
    AddStringObservation("a value2", 2, 1, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size,
                         expected_observation_num_bytes, EnvelopeMaker::kOk);
    expected_this_batch_size = 2;
    expected_observation_num_bytes = 29;
    AddStringObservation("Banana", 2, 2, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size,
                         expected_observation_num_bytes, EnvelopeMaker::kOk);

    // Add two more observations for metric 1
    expected_this_batch_index = 0;
    expected_this_batch_size = 3;
    expected_observation_num_bytes = 122;
    AddStringObservation("a value3", 1, 1, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size,
                         expected_observation_num_bytes, EnvelopeMaker::kOk);
    expected_this_batch_size = 4;
    expected_observation_num_bytes = 29;
    AddStringObservation("Banana", 1, 2, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size,
                         expected_observation_num_bytes, EnvelopeMaker::kOk);

    // Add two more observations for metric 2
    expected_this_batch_index = 1;
    expected_this_batch_size = 3;
    expected_observation_num_bytes = 123;
    AddStringObservation("a value40", 2, 1, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size,
                         expected_observation_num_bytes, EnvelopeMaker::kOk);
    expected_this_batch_size = 4;
    expected_observation_num_bytes = 29;
    AddStringObservation("Cantaloupe", 2, 2, expected_num_batches,
                         expected_this_batch_index, expected_this_batch_size,
                         expected_observation_num_bytes, EnvelopeMaker::kOk);

    // Make the encrypted Envelope.
    EncryptedMessage encrypted_message;
    EXPECT_TRUE(envelope_maker_->MakeEncryptedEnvelope(&encrypted_message));

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
    FakeSystemData::CheckSystemProfile(recovered_envelope);
  }

 protected:
  FakeSystemData fake_system_data_;
  std::unique_ptr<EnvelopeMaker> envelope_maker_;
  std::shared_ptr<ProjectContext> project_;
  Encoder encoder_;
};

// We perform DoTest() three times with a Clear() between each turn.
// This last tests that Clear() works correctly.
TEST_F(EnvelopeMakerTest, TestAll) {
  for (int i = 0; i < 3; i++) {
    DoTest();
    envelope_maker_->Clear();
  }
}

// Tests the MergeOutOf() method.
TEST_F(EnvelopeMakerTest, MergeOutOf) {
  // Add metric 1 batch to EnvelopeMaker 1 with strings 0..9
  uint32_t metric_id = 1;
  int expected_num_batches = 1;
  size_t expected_this_batch_index = 0;
  int expected_this_batch_size = 0;
  AddManyStringsNoOp(0, 10, metric_id, expected_num_batches,
                     expected_this_batch_index, expected_this_batch_size);

  // Add metric 2 batch to EnvelopeMaker 1 with strings 0..9
  metric_id = 2;
  expected_num_batches = 2;
  expected_this_batch_index = 1;
  AddManyStringsNoOp(0, 10, metric_id, expected_num_batches,
                     expected_this_batch_index, expected_this_batch_size);

  // Take EnvelopeMaker 1 and create EnvelopeMaker 2.
  auto envelope_maker1 = ResetEnvelopeMaker();

  // Add metric 2 batch to EnvelopeMaker 2 with strings 10..19
  metric_id = 2;
  expected_num_batches = 1;
  expected_this_batch_index = 0;
  AddManyStringsNoOp(10, 20, metric_id, expected_num_batches,
                     expected_this_batch_index, expected_this_batch_size);

  // Add metric 3 to EnvelopeMaker 2 with strings 0..9
  metric_id = 3;
  expected_num_batches = 2;
  expected_this_batch_index = 1;
  AddManyStringsNoOp(0, 10, metric_id, expected_num_batches,
                     expected_this_batch_index, expected_this_batch_size);

  // Take EnvelopeMaker 2,
  auto envelope_maker2 = ResetEnvelopeMaker();

  // Now invoke MergeOutOf to merge EnvelopeMaker 2 into EnvelopeMaker 1.
  envelope_maker1->MergeOutOf(envelope_maker2.get());

  // EnvelopeMaker 2 should be empty.
  EXPECT_TRUE(envelope_maker2->Empty());

  // EnvelopeMaker 1 should have three batches for Metrics 1, 2, 3
  EXPECT_FALSE(envelope_maker1->Empty());
  ASSERT_EQ(3, envelope_maker1->envelope().batch_size());

  // Iterate through each of the batches and check it.
  for (uint index = 0; index < 3; index++) {
    // Batch 0 and 2 should have 10 encrypted observations and batch
    // 1 should have 20 because batch 1 from EnvelopeMaker 2 was merged
    // into batch 1 of EnvelopeMaker 1.
    auto& batch = envelope_maker1->envelope().batch(index);
    EXPECT_EQ(index + 1, batch.meta_data().metric_id());
    auto expected_num_observations = (index == 1 ? 20 : 10);
    ASSERT_EQ(expected_num_observations, batch.encrypted_observation_size());

    // Check each one of the observations.
    for (int i = 0; i < expected_num_observations; i++) {
      // Extract the serialized observation.
      auto& encrypted_message = batch.encrypted_observation(i);
      EXPECT_EQ(EncryptedMessage::NONE, encrypted_message.scheme());
      std::string serialized_observation = encrypted_message.ciphertext();
      Observation recovered_observation;
      ASSERT_TRUE(
          recovered_observation.ParseFromString(serialized_observation));

      // Check that it looks right.
      ASSERT_EQ(1u, recovered_observation.parts().size());
      auto iter = recovered_observation.parts().find("Part1");
      ASSERT_TRUE(iter != recovered_observation.parts().cend());
      const auto& part = iter->second;
      ASSERT_EQ(3u, part.encoding_config_id());
      ASSERT_TRUE(part.has_unencoded());

      // Check the string values. Batches 0 and 2 are straightforward. The
      // values should be {"value 0", "value 1", .. "value 9"}. But
      // batch 1 is more complicated. Because of the way merge is implemented
      // we expect to see:
      // {"value 0", "value 1", .. "value 9", "value 19",
      //                                           "value 18", ... "value 10"}
      // This is because when we merged batch 1 of Envelope 2 into batch
      // 1 of Envelope 1 we reversed the order of the observations in
      // Ennvelope 2.
      std::ostringstream stream;
      int expected_value_index = i;
      if (index == 1 && i >= 10) {
        expected_value_index = 29 - i;
      }
      stream << "value " << expected_value_index;
      auto expected_string_value = stream.str();
      EXPECT_EQ(expected_string_value,
                part.unencoded().unencoded_value().string_value());
    }
  }

  // Now we want to test that after the MergeOutOf() operation the EnvelopeMaker
  // is still usable. Put EnvelopeMaker 1 back as the test EnvelopeMaker.
  envelope_maker_ = std::move(envelope_maker1);

  // Add string observations 10..19 to metric ID 1 batches 1, 2 and 3.
  for (int metric_id = 1; metric_id <= 3; metric_id++) {
    expected_num_batches = 3;
    expected_this_batch_index = metric_id - 1;
    expected_this_batch_size = (metric_id == 2 ? 20 : 10);
    AddManyStringsNoOp(10, 20, metric_id, expected_num_batches,
                       expected_this_batch_index, expected_this_batch_size);
  }
}

// Tests that EnvelopeMaker returns kObservationTooBig when it is supposed to.
TEST_F(EnvelopeMakerTest, ObservationTooBig) {
  static const uint32_t kMetricId = 1;
  static const uint32_t kEncodingConfigId = 3;  // NoOp encoding.

  // Set max_bytes_each_observation = 105.
  ResetEnvelopeMaker(105);

  // Build an input string of length 75 bytes.
  std::string value("x", 75);

  size_t expected_observation_num_bytes = 75 + kNoOpEncodingByteOverhead;

  // Invoke AddStringObservation() and expect kOk
  int expected_num_batches = 1;
  size_t expected_this_batch_index = 0;
  int expected_this_batch_size = 1;
  AddStringObservation(value, kMetricId, kEncodingConfigId,
                       expected_num_batches, expected_this_batch_index,
                       expected_this_batch_size, expected_observation_num_bytes,
                       EnvelopeMaker::kOk);

  // Build an input string of length 101 bytes.
  value = std::string("x", 101);
  // We expect the Observation to not be added to the Envelope and so for
  // the Envelope size to not change.
  expected_observation_num_bytes = 0;

  // Invoke AddStringObservation() and expect kObservationTooBig
  AddStringObservation(value, kMetricId, kEncodingConfigId,
                       expected_num_batches, expected_this_batch_index,
                       expected_this_batch_size, expected_observation_num_bytes,
                       EnvelopeMaker::kObservationTooBig);

  // Build an input string of length 75 bytes again.
  value = std::string("x", 75);
  expected_observation_num_bytes = 75 + kNoOpEncodingByteOverhead;
  expected_this_batch_size = 2;
  // Invoke AddStringObservation() and expect kOk.
  AddStringObservation(value, kMetricId, kEncodingConfigId,
                       expected_num_batches, expected_this_batch_index,
                       expected_this_batch_size, expected_observation_num_bytes,
                       EnvelopeMaker::kOk);
}

// Tests that EnvelopeMaker returns kEnvelopeFull when it is supposed to.
TEST_F(EnvelopeMakerTest, EnvelopeFull) {
  static const uint32_t kMetricId = 1;
  static const uint32_t kEncodingConfigId = 3;  // NoOp encoding.

  // Set max_bytes_each_observation = 100, max_num_bytes=1000.
  ResetEnvelopeMaker(100, 1000);

  int expected_this_batch_size = 1;
  int expected_num_batches = 1;
  size_t expected_this_batch_index = 0;
  for (int i = 0; i < 19; i++) {
    // Build an input string of length 20 bytes.
    std::string value("x", 20);
    size_t expected_observation_num_bytes = 20 + kNoOpEncodingByteOverhead;

    // Invoke AddStringObservation() and expect kOk
    AddStringObservation(value, kMetricId, kEncodingConfigId,
                         expected_num_batches, expected_this_batch_index,
                         expected_this_batch_size++,
                         expected_observation_num_bytes, EnvelopeMaker::kOk);
  }
  EXPECT_EQ(950u, envelope_maker_->size());

  // If we try to add an observation of more than 100 bytes we should
  // get kObservationTooBig.
  std::string value("x", 101);
  // We expect the Observation to not be added to the Envelope and so for
  // the Envelope size to not change.
  size_t expected_observation_num_bytes = 0;
  AddStringObservation(
      value, kMetricId, kEncodingConfigId, expected_num_batches,
      expected_this_batch_index, expected_this_batch_size++,
      expected_observation_num_bytes, EnvelopeMaker::kObservationTooBig);

  // If we try to add an observation of 65 bytes we should
  // get kEnvelopeFull
  value = std::string("x", 65);
  AddStringObservation(
      value, kMetricId, kEncodingConfigId, expected_num_batches,
      expected_this_batch_index, expected_this_batch_size++,
      expected_observation_num_bytes, EnvelopeMaker::kEnvelopeFull);
}

}  // namespace encoder
}  // namespace cobalt
