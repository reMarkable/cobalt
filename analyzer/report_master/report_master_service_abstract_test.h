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

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_MASTER_SERVICE_ABSTRACT_TEST_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_MASTER_SERVICE_ABSTRACT_TEST_H_

#include "analyzer/report_master/report_master_service.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./observation.pb.h"
#include "analyzer/store/report_store_test_utils.h"
#include "encoder/client_secret.h"
#include "encoder/encoder.h"
#include "encoder/project_context.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/datetime_util.h"

// This file contains type-parameterized tests of ReportMasterService.
//
// We use C++ templates along with the macros TYPED_TEST_CASE_P and
// TYPED_TEST_P in order to define test templates that may be instantiated to
// to produce concrete tests that use various implementations of Datastore.
//
// See report_master_service_test.cc and
// report_master_service_emulator_test.cc for the concrete instantiations.
//
// NOTE: If you add a new test to this file you must add its name to the
// invocation REGISTER_TYPED_TEST_CASE_P macro at the bottom of this file.

namespace cobalt {
namespace analyzer {

static const uint32_t kCustomerId = 1;
static const uint32_t kProjectId = 1;
static const uint32_t kMetricId1 = 1;
static const uint32_t kMetricId2 = 2;
static const uint32_t kReportConfigId1 = 1;
static const uint32_t kReportConfigId2 = 2;
static const uint32_t kForculusEncodingConfigId = 1;
static const uint32_t kBasicRapporStringEncodingConfigId = 2;
static const uint32_t kBasicRapporIntEncodingConfigId = 3;
static const char kPartName1[] = "Part1";
static const char kPartName2[] = "Part2";
static const size_t kForculusThreshold = 20;

// This unix timestamp corresponds to Friday Dec 2, 2016 in UTC
static const time_t kSomeTimestamp = 1480647356;
// This is the day index for Friday Dec 2, 2016
static const uint32_t kDayIndex = 17137;
// We will use a StationaryClock with the time fixed to this time in order
// to test that time-related fields are set correctly by ReportMasterService.
static const int64_t kFixedTimeSeconds = 1234567;

static const char* kMetricConfigText = R"(
# Metric 1 has one string part and one integer part.
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
      data_type: INT
    }
  }
}

# Metric 2 has one string part and one integer part.
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
  parts {
    key: "Part2"
    value {
      data_type: INT
    }
  }
}

)";

static const char* kEncodingConfigText = R"(
# EncodingConfig 1 is Forculus.
element {
  customer_id: 1
  project_id: 1
  id: 1
  forculus {
    threshold: 20
  }
}

# EncodingConfig 2 is Basic RAPPOR with string candidates (non-stochastic)
element {
  customer_id: 1
  project_id: 1
  id: 2
  basic_rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    string_categories: {
      category: "Apple"
      category: "Banana"
      category: "Cantaloupe"
    }
  }
}

# EncodingConfig 3 is Basic RAPPOR with integer candidates (non-stochastic).
element {
  customer_id: 1
  project_id: 1
  id: 3
  basic_rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    int_range_categories: {
      first: 1
      last:  10
    }
  }
}

)";

static const char* kReportConfigText = R"(
# ReportConfig 1 specifies a report with one variable: part 1 of Metric 1.
element {
  customer_id: 1
  project_id: 1
  id: 1
  metric_id: 1
  variable {
    metric_part: "Part1"
  }
}

# ReportConfig 2 specifies a report with 2 variables: Both parts of Metric 2.
element {
  customer_id: 1
  project_id: 1
  id: 2
  metric_id: 2
  report_type: JOINT
  variable {
    metric_part: "Part1"
  }
  variable {
    metric_part: "Part2"
  }
}

)";

// An implementation of grpc::Writer that keeps a copy of each object
// written for later checking.
class TestingQueryReportsResponseWriter
    : public grpc::WriterInterface<QueryReportsResponse> {
 public:
  bool Write(const QueryReportsResponse& response,
             const grpc::WriteOptions& options) override {
    responses.emplace_back(response);
    return true;
  }

  std::vector<QueryReportsResponse> responses;
};

// ReportMasterServiceAbstractTest is templatized on the parameter
// |StoreFactoryClass| which must be the name of a class that contains the
// following method: static DataStore* NewStore()
// See MemoryStoreFactory in store/memory_store_test_helper.h and
// BigtableStoreEmulatorFactory in store/bigtable_emulator_helper.h.
template <class StoreFactoryClass>
class ReportMasterServiceAbstractTest : public ::testing::Test {
 protected:
  ReportMasterServiceAbstractTest()
      : data_store_(StoreFactoryClass::NewStore()),
        observation_store_(new store::ObservationStore(data_store_)),
        report_store_(new store::ReportStore(data_store_)),
        clock_(new util::StationaryClock()) {
    clock_->set_current_time_seconds(kFixedTimeSeconds);
    report_store_->set_clock(clock_);
  }

  void SetUp() {
    // Clear the DataStore.
    EXPECT_EQ(store::kOK,
              data_store_->DeleteAllRows(store::DataStore::kObservations));
    EXPECT_EQ(store::kOK,
              data_store_->DeleteAllRows(store::DataStore::kReportMetadata));
    EXPECT_EQ(store::kOK,
              data_store_->DeleteAllRows(store::DataStore::kReportRows));

    // Parse the metric config string
    auto metric_parse_result =
        config::MetricRegistry::FromString(kMetricConfigText, nullptr);
    EXPECT_EQ(config::kOK, metric_parse_result.second);
    std::shared_ptr<config::MetricRegistry> metric_registry(
        metric_parse_result.first.release());

    // Parse the encoding config string
    auto encoding_parse_result =
        config::EncodingRegistry::FromString(kEncodingConfigText, nullptr);
    EXPECT_EQ(config::kOK, encoding_parse_result.second);
    std::shared_ptr<config::EncodingRegistry> encoding_config_registry(
        encoding_parse_result.first.release());

    // Parse the report config string
    auto report_parse_result =
        config::ReportRegistry::FromString(kReportConfigText, nullptr);
    EXPECT_EQ(config::kOK, report_parse_result.second);
    std::shared_ptr<config::ReportRegistry> report_config_registry(
        report_parse_result.first.release());

    // Make a ProjectContext
    project_.reset(new encoder::ProjectContext(
        kCustomerId, kProjectId, metric_registry, encoding_config_registry));

    // Make an AnalyzerConfig
    std::shared_ptr<config::AnalyzerConfig> analyzer_config(
        new config::AnalyzerConfig(encoding_config_registry, metric_registry,
                                   report_config_registry));

    report_master_service_.reset(new ReportMasterService(
        0, observation_store_, report_store_, analyzer_config,
        grpc::InsecureServerCredentials()));

    report_master_service_->StartWorkerThread();
  }

  void WaitUntilIdle() { report_master_service_->WaitUntilIdle(); }

  // Makes an Observation with one string part and one int part, using the
  // two given values and the two given encodings for the given metric.
  std::unique_ptr<Observation> MakeObservation(std::string part1_value,
                                               int part2_value,
                                               uint32_t metric_id,
                                               uint32_t encoding_config_id1,
                                               uint32_t encoding_config_id2) {
    // Construct a new Encoder with a new client secret.
    encoder::Encoder encoder(project_,
                             encoder::ClientSecret::GenerateNewSecret());
    // Set a static current time so we know we have a static day_index.
    encoder.set_current_time(kSomeTimestamp);

    // Construct the two-part value to add.
    encoder::Encoder::Value value;
    value.AddStringPart(encoding_config_id1, kPartName1, part1_value);
    value.AddIntPart(encoding_config_id2, kPartName2, part2_value);

    // Encode an observation.
    encoder::Encoder::Result result = encoder.Encode(metric_id, value);
    EXPECT_EQ(encoder::Encoder::kOK, result.status);
    EXPECT_TRUE(result.observation.get() != nullptr);
    EXPECT_EQ(2, result.observation->parts_size());
    return std::move(result.observation);
  }

  // Adds to the ObservationStore |num_clients| two-part observations that each
  // encode the given two values using the given metric and the
  // given two encodings. Each Observation is generated as if from a different
  // client.
  void AddObservations(std::string part1_value, int part2_value,
                       uint32_t metric_id, uint32_t encoding_config_id1,
                       uint32_t encoding_config_id2, int num_clients) {
    std::vector<Observation> observations;
    for (int i = 0; i < num_clients; i++) {
      observations.emplace_back(*MakeObservation(part1_value, part2_value,
                                                 metric_id, encoding_config_id1,
                                                 encoding_config_id2));
    }
    ObservationMetadata metadata;
    metadata.set_customer_id(kCustomerId);
    metadata.set_project_id(kProjectId);
    metadata.set_metric_id(metric_id);
    metadata.set_day_index(kDayIndex);
    EXPECT_EQ(store::kOK,
              observation_store_->AddObservationBatch(metadata, observations));
  }

  // Invokes ReportMaster::GetReport() and checks the returned ReportMetadata.
  void GetReportAndCheck(const std::string& report_id,
                         uint32_t expected_report_config_id, bool expect_part1,
                         bool expect_part2, bool check_completed,
                         Report* report_out) {
    GetReportRequest get_request;
    get_request.set_report_id(report_id);
    auto status =
        report_master_service_->GetReport(nullptr, &get_request, report_out);
    EXPECT_TRUE(status.ok()) << "error_code=" << status.error_code()
                             << " error_message=" << status.error_message();

    // Check report metadata
    CheckMetadata(report_id, expected_report_config_id, expect_part1,
                  expect_part2, check_completed, kFixedTimeSeconds,
                  report_out->metadata());
  }

  // Checks a ReportMetadata returned from GetReport or QueryReports.
  void CheckMetadata(const std::string& report_id,
                     uint32_t expected_report_config_id, bool expect_part1,
                     bool expect_part2, bool check_completed,
                     int64_t expected_current_time_seconds,
                     const ReportMetadata& metadata) {
    EXPECT_EQ(report_id, metadata.report_id());
    EXPECT_EQ(kCustomerId, metadata.customer_id());
    EXPECT_EQ(kProjectId, metadata.project_id());
    EXPECT_EQ(expected_report_config_id, metadata.report_config_id());
    EXPECT_EQ(expected_current_time_seconds,
              metadata.creation_time().seconds());

    bool expect_joint_report = expect_part1 && expect_part2;

    if (check_completed) {
      // Currently JOINT reports are not implemented so we expect the report
      // to have failed.
      ReportState expected_completion_state =
          (expect_joint_report ? TERMINATED : COMPLETED_SUCCESSFULLY);
      EXPECT_EQ(expected_completion_state, metadata.state());
      EXPECT_TRUE(metadata.start_time().seconds() >=
                  metadata.creation_time().seconds());
      EXPECT_TRUE(metadata.finish_time().seconds() >=
                  metadata.start_time().seconds());
    }

    EXPECT_EQ(kDayIndex, metadata.first_day_index());
    EXPECT_EQ(kDayIndex, metadata.last_day_index());

    // Check the metric parts.
    size_t expected_num_parts = (expect_joint_report ? 2 : 1);
    ASSERT_EQ(expected_num_parts, metadata.metric_parts_size());
    if (expect_part1) {
      EXPECT_EQ("Part1", metadata.metric_parts(0));
    } else if (!expect_part1 && expect_part2) {
      EXPECT_EQ("Part2", metadata.metric_parts(0));
    }
    if (expect_joint_report) {
      EXPECT_EQ("Part2", metadata.metric_parts(1));
    }

    // Check the associated_report_ids.
    if (expect_joint_report) {
      EXPECT_EQ(2, metadata.associated_report_ids_size());
    } else {
      EXPECT_EQ(0, metadata.associated_report_ids_size());
    }

    EXPECT_TRUE(metadata.one_off());

    // Check info_messages.
    if (check_completed && expect_joint_report) {
      ASSERT_NE(0, metadata.info_messages_size());
      EXPECT_NE(-1, metadata.info_messages(0).message().find(
                        "Report type JOINT is not yet implemented"));
      EXPECT_EQ(expected_current_time_seconds,
                metadata.info_messages(0).timestamp().seconds());
    }
  }

  // Invokes GetReportAndCheck() on the given joint report. Then extracts the
  // IDs of the two marginal reports and invokes GetReportAndCheck() on those
  // also.
  void CheckJointReportAndTwoMarginals(std::string report_id_joint,
                                       uint32_t expected_report_config_id,
                                       bool check_completed,
                                       Report* first_marginal_report_out,
                                       Report* second_marginal_report_out) {
    // Get and check the metadata of the joint report.
    Report joint_report;
    bool expect_part1 = true;
    bool expect_part2 = true;
    this->GetReportAndCheck(report_id_joint, expected_report_config_id,
                            expect_part1, expect_part2, check_completed,
                            &joint_report);
    // Currently joint reports are not yet implemented so there should be
    // no report rows.
    EXPECT_FALSE(joint_report.has_rows());

    // Extract the IDs of the two marginal reports.
    std::string report_id_11 = joint_report.metadata().associated_report_ids(0);
    std::string report_id_12 = joint_report.metadata().associated_report_ids(1);

    // Get and check the metadata of the first marginal report.
    expect_part1 = true;
    expect_part2 = false;
    this->GetReportAndCheck(report_id_11, expected_report_config_id,
                            expect_part1, expect_part2, check_completed,
                            first_marginal_report_out);
    if (check_completed) {
      EXPECT_TRUE(first_marginal_report_out->has_rows());
    }

    // Get and check the metadata of the second marginal report.
    expect_part1 = false;
    expect_part2 = true;
    this->GetReportAndCheck(report_id_12, expected_report_config_id,
                            expect_part1, expect_part2, check_completed,
                            second_marginal_report_out);
    if (check_completed) {
      EXPECT_TRUE(second_marginal_report_out->has_rows());
    }
  }

  // Invokes ReportMaster::QueryReportsInternal() using our fixed customer and
  // project and the given report_config_id and time interval. The responses
  // will be written to the given |response_writer|. Returns true for success
  // or false for failure.
  bool QueryReports(uint32_t report_config_id, uint64_t first_time_seconds,
                    uint64_t limit_time_seconds,
                    TestingQueryReportsResponseWriter* response_writer) {
    QueryReportsRequest request;
    request.set_customer_id(kCustomerId);
    request.set_project_id(kProjectId);
    request.set_report_config_id(report_config_id);
    request.mutable_first_timestamp()->set_seconds(first_time_seconds);
    request.mutable_limit_timestamp()->set_seconds(limit_time_seconds);
    auto status = report_master_service_->QueryReportsInternal(
        nullptr, &request, response_writer);
    EXPECT_TRUE(status.ok()) << "error_code=" << status.error_code()
                             << " error_message=" << status.error_message();
    return status.ok();
  }

  void set_current_time_seconds(int64_t current_time_seconds) {
    clock_->set_current_time_seconds(current_time_seconds);
  }

  // Writes Metadata directly into the ReportStore simulating the case that
  // StarReport() was invoked many times to form |num_reports| different
  // instances of the report with the given |report_config_id|.
  //
  // The creation time and start time for report i will be
  // kFixedTimeSeconds + i.
  //
  // The implementation of this function breaks several layers of abstraction
  // and writes directly into the underlying ReporStore. This is a convenient
  // way to efficiently set up the ReportMetadata table in order test the
  // QueryReports function. If we were to use the gRPC API to accomplish this
  // it would require many RPC roundtrips which would take a long time.
  // There is no reason for the gRPC API to support an efficient implementation
  // of this function as it is not useful outside of a test.
  //
  // The vector of string report IDs from the gRPC API are returned so that
  // they may be used to query in the gRPC API.
  std::vector<std::string> WriteManyNewReports(uint64_t report_config_id,
                                               size_t num_reports) {
    ReportId report_id;
    report_id.set_customer_id(kCustomerId);
    report_id.set_project_id(kProjectId);
    report_id.set_report_config_id(report_config_id);
    std::vector<ReportId> report_ids(num_reports, report_id);

    ReportMetadataLite metadata;
    metadata.set_state(IN_PROGRESS);
    metadata.set_first_day_index(kDayIndex);
    metadata.set_last_day_index(kDayIndex);
    metadata.set_one_off(true);
    metadata.add_variable_indices(0);
    std::vector<ReportMetadataLite> report_metadata(num_reports, metadata);

    std::vector<std::string> string_report_ids(num_reports);
    for (int i = 0; i < num_reports; i++) {
      report_ids[i].set_creation_time_seconds(kFixedTimeSeconds + i);
      report_ids[i].set_instance_id(i);
      report_metadata[i].set_start_time_seconds(kFixedTimeSeconds + i);
      string_report_ids[i] =
          report_master_service_->MakeStringReportId(report_ids[i]);
    }
    // We write all the reports with a single RPC.
    store::ReportStoreTestUtils test_utils(report_store_);
    EXPECT_EQ(store::kOK,
              test_utils.WriteBulkMetadata(report_ids, report_metadata));
    return string_report_ids;
  }

  std::shared_ptr<encoder::ProjectContext> project_;
  std::shared_ptr<store::DataStore> data_store_;
  std::shared_ptr<store::ObservationStore> observation_store_;
  std::shared_ptr<store::ReportStore> report_store_;
  std::unique_ptr<ReportMasterService> report_master_service_;
  std::shared_ptr<util::StationaryClock> clock_;
};

TYPED_TEST_CASE_P(ReportMasterServiceAbstractTest);

// Adds observations to the ObservationStore and then uses the ReportMaster
// to run two reports for our two registered ReportConfigs. Checks the
// results. From the ReportMaster API we test the methods StartReport and
// GetReport.
TYPED_TEST_P(ReportMasterServiceAbstractTest, StartAndGetReports) {
  // Add some observations for metric 1. We use Basic RAPPOR for
  // both parts. We add 20 observations of the pair ("Apple", 10).
  // Our report will only analyze part 1; part 2 will be ignored. We have
  // set the RAPPOR parameters p and q so there is no randomness. We therefore
  // will expect the report to produce the following results:
  // ("Apple", 20), ("Banana", 0), ("Cantaloupe", 0).
  this->AddObservations("Apple", 10, kMetricId1,
                        kBasicRapporStringEncodingConfigId,
                        kBasicRapporIntEncodingConfigId, 20);

  // Add some observations for metric 2. We use Forculus for part 1
  // and BasicRappor for part 2. For the Forculus part there will be
  // 20 observations of "Apple", 19 observations of "Banana", and 21
  // observations of "Cantaloupe" so we expect to see "Apple" and "Cantaloupe"
  // in the report but not "Banana". For the Basic RAPPOR part there will
  // be 20 observations of |10|, 19 observations of |9|, and 21 observations
  // of |8|. Joint reports are not implemented yet so we will only be checking
  // the results of the two marginal reports.
  this->AddObservations("Apple", 10, kMetricId2, kForculusEncodingConfigId,
                        kBasicRapporIntEncodingConfigId, kForculusThreshold);
  this->AddObservations("Banana", 9, kMetricId2, kForculusEncodingConfigId,
                        kBasicRapporIntEncodingConfigId,
                        kForculusThreshold - 1);
  this->AddObservations("Cantaloupe", 8, kMetricId2, kForculusEncodingConfigId,
                        kBasicRapporIntEncodingConfigId,
                        kForculusThreshold + 1);

  // Start the first report. This is a one-variable report of
  // part 1 of metric 1.
  StartReportRequest start_request;
  start_request.set_customer_id(kCustomerId);
  start_request.set_project_id(kProjectId);
  start_request.set_report_config_id(kReportConfigId1);
  start_request.set_first_day_index(kDayIndex);
  start_request.set_last_day_index(kDayIndex);
  StartReportResponse start_response;
  auto status = this->report_master_service_->StartReport(
      nullptr, &start_request, &start_response);
  EXPECT_TRUE(status.ok()) << "error_code=" << status.error_code()
                           << " error_message=" << status.error_message();
  // Capture the ID for report 1.
  std::string report_id1 = start_response.report_id();
  EXPECT_FALSE(report_id1.empty());

  // Start the second report. This is a joint two-variable report of metric 2.
  // The two marginal reports will be automatically started also but the
  // returned report_id will be for the joint report. Since joint reports are
  // not implemented yet we will only be checking the results of the two
  // marginal reports but we will be checking the metadata of the joint
  // report too.
  start_request.set_report_config_id(kReportConfigId2);
  status = this->report_master_service_->StartReport(nullptr, &start_request,
                                                     &start_response);
  EXPECT_TRUE(status.ok()) << "error_code=" << status.error_code()
                           << " error_message=" << status.error_message();
  // Capture the ID for report 2. This is the ID of the joint report.
  std::string report_id2 = start_response.report_id();
  EXPECT_FALSE(report_id2.empty());

  // Check the meta-data of the first report. It should include part 1 and
  // not part 2.
  Report report1;
  bool expect_part1 = true;
  bool expect_part2 = false;
  // The report is generated asynchronously and we don't know that it is
  // done yet so don't check that it is completed.
  bool check_completed = false;
  {
    SCOPED_TRACE("");
    this->GetReportAndCheck(report_id1, kReportConfigId1, expect_part1,
                            expect_part2, check_completed, &report1);
  }

  // Check the meta-data of the second report. We should find a joint report
  // and two associated marginal reports and we check the metadata of all three.
  // The joint report will have meta-data only because joint reports are not
  // implemented yet. But the two marginals will be returned to us so we can
  // check them. (But not yet because we don't know that the report generation
  // is completed yet.)
  Report first_marginal_report;
  Report second_marginal_report;
  {
    SCOPED_TRACE("");
    this->CheckJointReportAndTwoMarginals(
        report_id2, kReportConfigId2, check_completed, &first_marginal_report,
        &second_marginal_report);
  }

  // Wait until the report generation for all reports completes.
  this->WaitUntilIdle();

  // Check the reports again but this time check that they are completed
  // and then check the actual contents of the report rows.
  check_completed = true;
  {
    SCOPED_TRACE("");
    this->GetReportAndCheck(report_id1, kReportConfigId1, expect_part1,
                            expect_part2, check_completed, &report1);
  }

  // Check the rows of report 1.
  // Recall that when adding observations to metric 1 above we used Basic
  // RAPPOR with no randomness so we expect to see the results
  // ("Apple", 20), ("Banana", 0), ("Cantaloupe", 0).
  ASSERT_EQ(3, report1.rows().rows_size());
  std::map<std::string, int> report1_results;
  for (int i = 0; i < 3; i++) {
    report1_results[report1.rows().rows(i).histogram().value().string_value()] =
        report1.rows().rows(i).histogram().count_estimate();
  }
  ASSERT_EQ(3, report1_results.size());
  EXPECT_EQ(20, report1_results["Apple"]);
  EXPECT_EQ(0, report1_results["Banana"]);
  EXPECT_EQ(0, report1_results["Cantaloupe"]);

  // Check report 2 again including its associated marginal reports, this
  // time checking that they are complete.
  {
    SCOPED_TRACE("");
    this->CheckJointReportAndTwoMarginals(
        report_id2, kReportConfigId2, check_completed, &first_marginal_report,
        &second_marginal_report);
  }

  // Check the rows of the first marginal of report 2. Recall that when adding
  // rows to part 1 of metric 2 above we used Forculus and we expect to see the
  // results ("Apple", 20), ("Cantaloupe", 21) and not to see "Banana" because
  // it should not have been decrypted.
  ASSERT_EQ(2, first_marginal_report.rows().rows_size());
  std::map<std::string, int> first_marginal_results;
  for (int i = 0; i < 2; i++) {
    first_marginal_results[first_marginal_report.rows()
                               .rows(i)
                               .histogram()
                               .value()
                               .string_value()] =
        first_marginal_report.rows().rows(i).histogram().count_estimate();
  }
  ASSERT_EQ(2, first_marginal_results.size());
  EXPECT_EQ(20, first_marginal_results["Apple"]);
  EXPECT_EQ(21, first_marginal_results["Cantaloupe"]);

  // Check the rows of the second marginal of report 2. Recall that when adding
  // rows to part 2 of metric 2 above we used Basic RAPPOR with no randomness
  // so we expect to see the following results:
  // (a) A count of 0 for the numbers 1, 2, 3, 4, 5, 6, 7
  // (b) (8, 21), (9, 19), (10, 20)
  ASSERT_EQ(10, second_marginal_report.rows().rows_size());
  std::map<int, int> second_marginal_results;
  for (int i = 0; i < 10; i++) {
    second_marginal_results[second_marginal_report.rows()
                                .rows(i)
                                .histogram()
                                .value()
                                .int_value()] =
        second_marginal_report.rows().rows(i).histogram().count_estimate();
  }
  ASSERT_EQ(10, second_marginal_results.size());
  for (int i = 1; i <= 7; i++) {
    EXPECT_EQ(0, second_marginal_results[i]);
  }
  EXPECT_EQ(21, second_marginal_results[8]);
  EXPECT_EQ(19, second_marginal_results[9]);
  EXPECT_EQ(20, second_marginal_results[10]);
}

// Tests the method ReportMaster::QueryReports. We write into the ReportStore
// many instance of ReportConfig 1 and then invoke QueryReports() and check the
// results.
TYPED_TEST_P(ReportMasterServiceAbstractTest, QueryReportsTest) {
  // Write Metadata into the ReportStore for 210 reports associated with
  // ReporcConfig 1  with creation_times that start at kFixedTimeSeconds and
  // increment by 1 second for each report.
  std::vector<std::string> report_ids =
      this->WriteManyNewReports(kReportConfigId1, 210);

  // Now invoke QueryReports. We specify a time window that will omit the
  // first three and the last 3 reports. So there should be 204 reports
  // returned.
  TestingQueryReportsResponseWriter response_writer;
  ASSERT_TRUE(this->QueryReports(kReportConfigId1, kFixedTimeSeconds + 3,
                                 kFixedTimeSeconds + 207, &response_writer));

  // Since we know that reports are returned in batches of 100 we expect there
  // to be 3 batches: Two batches of size 100 and one batch of size 4.
  EXPECT_EQ(3, response_writer.responses.size());

  // Check the first batch.
  bool expect_part1 = true;
  bool expect_part2 = false;
  // We don't know that the reports are completed yet.
  bool check_completed = false;
  EXPECT_EQ(100, response_writer.responses[0].reports_size());
  for (int i = 0; i < 100; i++) {
    this->CheckMetadata(report_ids[i + 3], kReportConfigId1, expect_part1,
                        expect_part2, check_completed,
                        kFixedTimeSeconds + 3 + i,
                        response_writer.responses[0].reports(i));
  }

  // Check the second batch.
  EXPECT_EQ(100, response_writer.responses[1].reports_size());
  for (int i = 0; i < 100; i++) {
    this->CheckMetadata(report_ids[i + 103], kReportConfigId1, expect_part1,
                        expect_part2, check_completed,
                        kFixedTimeSeconds + 103 + i,
                        response_writer.responses[1].reports(i));
  }

  // Check the third batch.
  EXPECT_EQ(4, response_writer.responses[2].reports_size());
  for (int i = 0; i < 4; i++) {
    this->CheckMetadata(report_ids[i + 203], kReportConfigId1, expect_part1,
                        expect_part2, check_completed,
                        kFixedTimeSeconds + 203 + i,
                        response_writer.responses[2].reports(i));
  }
}

REGISTER_TYPED_TEST_CASE_P(ReportMasterServiceAbstractTest, StartAndGetReports,
                           QueryReportsTest);

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_MASTER_SERVICE_ABSTRACT_TEST_H_
