// Copyright 2017 The Fuchsia Authors
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

#include "analyzer/report_master/encoding_mixer.h"

#include <string>
#include <utility>

#include "./observation.pb.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/project_context.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {

using config::AnalyzerConfig;
using config::EncodingRegistry;
using config::MetricRegistry;
using encoder::ClientSecret;
using encoder::Encoder;
using encoder::ProjectContext;

namespace {

const uint32_t kCustomerId = 1;
const uint32_t kProjectId = 1;
const uint32_t kMetricId = 1;
const uint32_t kForculusEncodingConfigId = 1;
const uint32_t kBasicRapporEncodingConfigId = 2;
const char kPartName1[] = "Part1";
const char kPartName2[] = "Part2";
const size_t kForculusThreshold = 20;

// This unix timestamp corresponds to Friday Dec 2, 2016 in UTC
const time_t kSomeTimestamp = 1480647356;
// This is the day index for Friday Dec 2, 2016
const uint32_t kDayIndex = 17137;

const char* kMetricConfigText = R"(
# Metric 1 has two string parts.
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
  parts {
    key: "Part2"
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

# EncodingConfig 2 is Basic RAPPOR.
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

}  // namespace

class EncodingMixerTest : public ::testing::Test {
 protected:
  // Sets up the test for the given variable_slice. The slice appears as
  // part of the report_id and the report_id is passed in to the
  // constructor of the EncodingMixer so the EncodingMixer knows which
  // variable slice it is for.
  void SetUpForSlice(int slice_index) {
    report_id_.set_customer_id(kCustomerId);
    report_id_.set_project_id(kProjectId);
    report_id_.set_variable_slice((VariableSlice)slice_index);

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
        (encoding_parse_result.first.release()));

    project_.reset(new ProjectContext(kCustomerId, kProjectId, metric_registry,
                                      encoding_registry));

    std::shared_ptr<AnalyzerConfig> analyzer_config(
        new AnalyzerConfig(encoding_registry, metric_registry, nullptr));

    encoding_mixer_.reset(new EncodingMixer(report_id_, analyzer_config));
  }

  // Makes an Observation with two string parts, both of which have the
  // given |string_value|, using the encoding with the given encoding_config_id.
  std::unique_ptr<Observation> MakeObservation(std::string string_value,
                                               uint32_t encoding_config_id) {
    // Construct a new Encoder with a new client secret.
    Encoder encoder(project_, ClientSecret::GenerateNewSecret());
    // Set a static current time so we know we have a static day_index.
    encoder.set_current_time(kSomeTimestamp);

    Encoder::Value value;
    value.AddStringPart(encoding_config_id, kPartName1, string_value);
    value.AddStringPart(encoding_config_id, kPartName2, string_value);

    // Encode an observation.
    Encoder::Result result = encoder.Encode(kMetricId, value);
    EXPECT_EQ(Encoder::kOK, result.status);
    EXPECT_TRUE(result.observation.get() != nullptr);
    EXPECT_EQ(2, result.observation->parts_size());
    return std::move(result.observation);
  }

  // Makes an Observation with two string parts, both of which have the
  // given |value|, using the encoding with the given encoding_config_id.
  // Then passes one of the two parts into
  // EncodingMixer::ProcessObservationPart().
  bool MakeAndProcessObservationPart(std::string value,
                                     uint32_t encoding_config_id,
                                     std::string part_name) {
    std::unique_ptr<Observation> observation =
        MakeObservation(value, encoding_config_id);
    return encoding_mixer_->ProcessObservationPart(
        kDayIndex, observation->parts().at(part_name));
  }

  // Invokes MakeAndProcessObservationPart many times using the Forculus
  // encoding. Three strings are encoded: "hello" 20 times,
  // "goodbye" 19 times and "peace" 21 times. The first and third will be
  // decrypted by Forculus and the 2nd will not.
  void MakeAndProcessForculusObservations(const std::string part_name) {
    // Add the word "hello" 20 times.
    for (int i = 0; i < kForculusThreshold; i++) {
      EXPECT_TRUE(MakeAndProcessObservationPart(
          "hello", kForculusEncodingConfigId, part_name));
    }
    // Add the word "goodbye" 19 times.
    for (int i = 0; i < kForculusThreshold - 1; i++) {
      EXPECT_TRUE(MakeAndProcessObservationPart(
          "goodbye", kForculusEncodingConfigId, part_name));
    }
    // Add the word "peace" 21 times.
    for (int i = 0; i < kForculusThreshold + 1; i++) {
      EXPECT_TRUE(MakeAndProcessObservationPart(
          "peace", kForculusEncodingConfigId, part_name));
    }
  }

  // Tests the EncodingMixer when it is used on a homogeneous set of
  // Observations, all of which were encoded using the same Forculus
  // EncodingCofig. This can be done on VARIABLE_1 or VARIABLE_2
  void DoForculusTest(int slice_index) {
    SetUpForSlice(slice_index);
    std::string part_name = (slice_index == 0 ? kPartName1 : kPartName2);
    MakeAndProcessForculusObservations(part_name);

    // Perform the analysis.
    std::vector<ReportRow> report_rows;
    EXPECT_TRUE(encoding_mixer_->PerformAnalysis(&report_rows).ok());

    // Check the results.
    EXPECT_EQ(2, report_rows.size());
    for (const auto& report_row : report_rows) {
      EXPECT_EQ(0, report_row.std_error());
      ValuePart recovered_value;
      switch (slice_index) {
        case 0:
          EXPECT_FALSE(report_row.has_value2());
          EXPECT_TRUE(report_row.has_value());
          recovered_value = report_row.value();
          break;
        case 1:
          EXPECT_TRUE(report_row.has_value2());
          EXPECT_FALSE(report_row.has_value());
          recovered_value = report_row.value2();
          break;
        default:
          FAIL();
      }

      EXPECT_EQ(ValuePart::kStringValue, recovered_value.data_case());
      std::string string_value = recovered_value.string_value();
      int count_estimate = report_row.count_estimate();
      switch (count_estimate) {
        case 20:
          EXPECT_EQ("hello", string_value);
          break;
        case 21:
          EXPECT_EQ("peace", string_value);
          break;
        default:
          FAIL();
      }
    }
  }

  void MakeAndProcessBasicRapporObservations(const std::string part_name) {
    for (int i = 0; i < 100; i++) {
      EXPECT_TRUE(MakeAndProcessObservationPart(
          "Apple", kBasicRapporEncodingConfigId, part_name));
    }
    for (int i = 0; i < 200; i++) {
      EXPECT_TRUE(MakeAndProcessObservationPart(
          "Banana", kBasicRapporEncodingConfigId, part_name));
    }
    for (int i = 0; i < 300; i++) {
      EXPECT_TRUE(MakeAndProcessObservationPart(
          "Cantaloupe", kBasicRapporEncodingConfigId, part_name));
    }
  }

  void DoBasicRapporTest(int slice_index) {
    SetUpForSlice(slice_index);
    std::string part_name = (slice_index == 0 ? kPartName1 : kPartName2);
    MakeAndProcessBasicRapporObservations(part_name);

    // Perform the analysis.
    std::vector<ReportRow> report_rows;
    EXPECT_TRUE(encoding_mixer_->PerformAnalysis(&report_rows).ok());

    // Check the results.
    EXPECT_EQ(3, report_rows.size());
    for (const auto& report_row : report_rows) {
      EXPECT_NE(0, report_row.std_error());
      ValuePart recovered_value;
      switch (slice_index) {
        case 0:
          EXPECT_FALSE(report_row.has_value2());
          EXPECT_TRUE(report_row.has_value());
          recovered_value = report_row.value();
          break;
        case 1:
          EXPECT_TRUE(report_row.has_value2());
          EXPECT_FALSE(report_row.has_value());
          recovered_value = report_row.value2();
          break;
        default:
          FAIL();
      }

      EXPECT_EQ(ValuePart::kStringValue, recovered_value.data_case());
      std::string string_value = recovered_value.string_value();
      EXPECT_TRUE(string_value == "Apple" || string_value == "Banana" ||
                  string_value == "Cantaloupe");

      EXPECT_GT(report_row.count_estimate(), 0);
    }
  }

  void DoMixedEncodingTest(int slice_index) {
    SetUpForSlice(slice_index);
    std::string part_name = (slice_index == 0 ? kPartName1 : kPartName2);
    MakeAndProcessForculusObservations(part_name);
    MakeAndProcessBasicRapporObservations(part_name);

    // Perform the analysis.
    std::vector<ReportRow> report_rows;
    EXPECT_EQ(grpc::UNIMPLEMENTED,
              encoding_mixer_->PerformAnalysis(&report_rows).error_code());
  }

  ReportId report_id_;
  std::shared_ptr<ProjectContext> project_;
  std::unique_ptr<EncodingMixer> encoding_mixer_;
};

TEST_F(EncodingMixerTest, Forculus) {
  {
    SCOPED_TRACE("slice_index = 0");
    int slice_index = 0;
    DoForculusTest(slice_index);
  }
  {
    SCOPED_TRACE("slice_index = 1");
    int slice_index = 1;
    DoForculusTest(slice_index);
  }
}

TEST_F(EncodingMixerTest, BasicRappor) {
  {
    SCOPED_TRACE("slice_index = 0");
    int slice_index = 0;
    DoBasicRapporTest(slice_index);
  }
  {
    SCOPED_TRACE("slice_index = 1");
    int slice_index = 1;
    DoBasicRapporTest(slice_index);
  }
}

TEST_F(EncodingMixerTest, MixedEncoding) {
  {
    SCOPED_TRACE("slice_index = 0");
    int slice_index = 0;
    DoMixedEncodingTest(slice_index);
  }
  {
    SCOPED_TRACE("slice_index = 1");
    int slice_index = 1;
    DoMixedEncodingTest(slice_index);
  }
}

}  // namespace analyzer
}  // namespace cobalt

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
