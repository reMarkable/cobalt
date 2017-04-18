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

#include "analyzer/report_master/report_generator.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/report_master/encoding_mixer.h"
#include "glog/logging.h"

namespace cobalt {
namespace analyzer {

using config::MetricRegistry;
using config::ReportRegistry;
using forculus::ForculusAnalyzer;
using store::ObservationStore;
using store::ReportStore;
using store::Status;

namespace {
std::string ThreePartIdString(const std::string& prefix, uint32_t a, uint32_t b,
                              uint32_t c) {
  std::ostringstream stream;
  stream << prefix << "(" << a << "," << b << "," << c << ")";
  return stream.str();
}

// Returns a human-readable string that identifies the report_config_id
// within the |report_id|.
std::string ReportConfigIdString(const ReportId& report_id) {
  return ThreePartIdString("report_config_id=", report_id.customer_id(),
                           report_id.project_id(),
                           report_id.report_config_id());
}

// Returns a human-readable string that identifies the metric_id
// within the |report_config|.
std::string MetricIdString(const ReportConfig& report_config) {
  return ThreePartIdString("metric_id=", report_config.customer_id(),
                           report_config.project_id(),
                           report_config.metric_id());
}

// Checks the status returned from GetMetadata(). If not kOK, does LOG(ERROR)
// and returns an appropriate grpc::Status.
grpc::Status CheckStatusFromGet(Status status, const ReportId& report_id) {
  switch (status) {
    case store::kOK:
      return grpc::Status::OK;

    case store::kNotFound: {
      std::ostringstream stream;
      stream << "No report found with id=" << ReportStore::ToString(report_id);
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::NOT_FOUND, message);
    }

    default: {
      std::ostringstream stream;
      stream << "GetMetadata failed with status=" << status
             << " for report_id=" << ReportStore::ToString(report_id);
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::ABORTED, message);
    }
  }
}

// Builds the appropriate vector of MetricPart names to analyze given the
// |report_id| and the |report_config|. Writes the result into |parts|.
// On error, does LOG(ERROR) and returns an appropriate status.
grpc::Status BuildParts(const ReportConfig& report_config,
                        const ReportId& report_id,
                        std::vector<std::string>* parts) {
  switch (report_id.variable_slice()) {
    case VARIABLE_1:
      parts->emplace_back(report_config.variable(0).metric_part());
      return grpc::Status::OK;
    case VARIABLE_2:
      if (!(report_config.variable_size() >= 2)) {
        std::ostringstream stream;
        stream << "Invalid arguments: report_id.variable_slice specifies "
                  "VARIABLE_2 but "
                  "ReportConfig has only one variable. "
               << ReportConfigIdString(report_id)
               << " report_id=" << ReportStore::ToString(report_id);
        std::string message = stream.str();
        LOG(ERROR) << message;
        return grpc::Status(grpc::INVALID_ARGUMENT, message);
      }
      parts->emplace_back(report_config.variable(1).metric_part());
      return grpc::Status::OK;
    case JOINT: {
      std::ostringstream stream;
      if (!(report_config.variable_size() >= 2)) {
        stream << "Invalid arguments: report_id.variable_slice specifies JOINT "
                  "but ReportConfig has only one variable. "
               << ReportConfigIdString(report_id)
               << " report_id=" << ReportStore::ToString(report_id);
        std::string message = stream.str();
        LOG(ERROR) << message;
        return grpc::Status(grpc::INVALID_ARGUMENT, message);
      }
      parts->emplace_back(report_config.variable(0).metric_part());
      parts->emplace_back(report_config.variable(1).metric_part());

      // TODO(rudominer) Implement joint two-varaible reports.
      stream << "JOINT report processing not currently implemented "
             << ReportConfigIdString(report_id);
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::UNIMPLEMENTED, message);
    }
    default:
      LOG(FATAL) << "Unexpected value for enum variable_slice: "
                 << report_id.variable_slice();
  }
}

}  // namespace

ReportGenerator::ReportGenerator(
    std::shared_ptr<config::AnalyzerConfig> analyzer_config,
    std::shared_ptr<ObservationStore> observation_store,
    std::shared_ptr<ReportStore> report_store)
    : analyzer_config_(analyzer_config),
      observation_store_(observation_store),
      report_store_(report_store) {}

grpc::Status ReportGenerator::GenerateReport(const ReportId& report_id) {
  // Fetch ReportMetadata
  ReportMetadataLite metadata;
  auto status = CheckStatusFromGet(
      report_store_->GetMetadata(report_id, &metadata), report_id);
  if (!status.ok()) {
    return status;
  }

  // Report must be IN_PROGRESS
  if (metadata.state() != IN_PROGRESS) {
    std::ostringstream stream;
    stream << "Report is not IN_PROGRESS" << ReportStore::ToString(report_id);
    std::string message = stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::FAILED_PRECONDITION, message);
  }

  // Fetch ReportConfig
  const ReportConfig* report_config = analyzer_config_->ReportConfig(
      report_id.customer_id(), report_id.project_id(),
      report_id.report_config_id());
  if (!report_config) {
    std::ostringstream stream;
    stream << "Not found: " << ReportConfigIdString(report_id);
    std::string message = stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::NOT_FOUND, message);
  }

  // ReportConfig must be valid.
  if (report_config->variable_size() == 0) {
    std::ostringstream stream;
    stream << "Invalid ReportConfig, no variables. "
           << ReportConfigIdString(report_id);
    std::string message = stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::INVALID_ARGUMENT, message);
  }

  // Fetch the Metric.
  const Metric* metric = analyzer_config_->Metric(report_config->customer_id(),
                                                  report_config->project_id(),
                                                  report_config->metric_id());
  if (!metric) {
    std::ostringstream stream;
    stream << "Not found: " << MetricIdString(*report_config);
    std::string message = stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::NOT_FOUND, message);
  }

  // Determine which variable slice we are doing and create corresponding
  // metric parts vector.
  std::vector<std::string> parts;
  status = BuildParts(*report_config, report_id, &parts);
  if (!status.ok()) {
    return status;
  }

  // Check that each of the part names are valid.
  for (auto& part : parts) {
    if (metric->parts().find(part) == metric->parts().end()) {
      std::ostringstream stream;
      stream << "Invalid ReportConfig: variable part name '" << part
             << "' is not the name of a part of the metric with "
             << MetricIdString(*report_config) << ". "
             << ReportConfigIdString(report_id);
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
  }

  // Note(rudominer) Because joint reports are not yet implemented we know
  // now that we must generate a single-variable report.
  return GenerateSingleVariableReport(
      report_id, *report_config, *metric, std::move(parts),
      metadata.first_day_index(), metadata.last_day_index());
}

grpc::Status ReportGenerator::GenerateSingleVariableReport(
    const ReportId& report_id, const ReportConfig& report_config,
    const Metric& metric, std::vector<std::string> single_part,
    uint32_t start_day_index, uint32_t end_day_index) {
  CHECK_EQ(1, single_part.size());
  DCHECK(start_day_index <= end_day_index);
  const std::string& part_name = single_part[0];

  // Construct the EncodingMixer.
  EncodingMixer encoding_mixer(report_id, analyzer_config_);

  // We query the ObservationStore for the relevant ObservationParts.
  store::ObservationStore::QueryResponse query_response;
  query_response.pagination_token = "";
  // We iteratively query in batches of size 1000.
  static const size_t kMaxResultsPerIteration = 1000;
  do {
    VLOG(4) << "Querying for 1000 observations...";
    query_response = observation_store_->QueryObservations(
        report_config.customer_id(), report_config.project_id(),
        report_config.metric_id(), start_day_index, end_day_index, single_part,
        kMaxResultsPerIteration, query_response.pagination_token);

    if (query_response.status != store::kOK) {
      std::ostringstream stream;
      stream << "QueryObservations failed with status=" << query_response.status
             << " for report_id=" << ReportStore::ToString(report_id)
             << " part=" << part_name;
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::ABORTED, message);
    }

    VLOG(4) << "Got " << query_response.results.size() << " observations.";

    // Iterate through the received batch.
    for (const auto& query_result : query_response.results) {
      CHECK_EQ(1, query_result.observation.parts_size());
      const auto& observation_part =
          query_result.observation.parts().at(part_name);
      // Process each ObservationPart using the EncodingMixer.
      // TODO(rudominer) This method returns false when the Observation was
      // bad in some way. This should be kept track of through a monitoring
      // counter.
      encoding_mixer.ProcessObservationPart(query_result.day_index,
                                            observation_part);
    }
  } while (!query_response.pagination_token.empty());

  // Complete the analysis using the EncodingMixer.
  std::vector<ReportRow> report_rows;
  grpc::Status status = encoding_mixer.PerformAnalysis(&report_rows);
  if (!status.ok()) {
    return status;
  }

  VLOG(4) << "Generated report with " << report_rows.size() << " rows.";

  // Write the report rows to the ReportStore.
  auto store_status = report_store_->AddReportRows(report_id, report_rows);
  switch (store_status) {
    case store::kOK:
      break;

    case store::kInvalidArguments: {
      std::ostringstream stream;
      stream << "Internal error. ReportStore returned kInvalidArguments for "
                "report_id="
             << ReportStore::ToString(report_id);
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::INTERNAL, message);
    }

    default: {
      std::ostringstream stream;
      stream << "AddReportRows failed with status=" << store_status
             << " for report_id=" << ReportStore::ToString(report_id);
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::ABORTED, message);
    }
  }

  return grpc::Status::OK;
}

}  // namespace analyzer
}  // namespace cobalt
