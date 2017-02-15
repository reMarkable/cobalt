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

// The report_master periodically scans the database, decodes any observations,
// and
// publishes them.

#include "analyzer/report_master/report_master_service.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/report_master/report_executor.h"
#include "analyzer/report_master/report_generator.h"
#include "analyzer/store/data_store.h"
#include "config/analyzer_config.h"
#include "glog/logging.h"
#include "util/crypto_util/base64.h"

namespace cobalt {
namespace analyzer {

using config::AnalyzerConfig;
using crypto::Base64Decode;
using crypto::Base64Encode;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::WriteOptions;
using store::DataStore;
using store::ObservationStore;
using store::ReportStore;

DEFINE_int32(port, 0,
             "The port that the ReportMaster Service should listen on.");
DEFINE_string(ssl_cert_info, "", "TBD: Some info about SSL Certificates.");

namespace {
// Builds the string form of a report_id used in the public ReportMasterService
// API from the ReportId message used in the internal API to ReportStore.
grpc::Status ReportIdToString(const ReportId& report_id,
                              std::string* id_string_out) {
  std::string serialized_id;
  if (!report_id.SerializeToString(&serialized_id)) {
    // Note(rudominer) This is just for completeness. I expect this to never
    // happen.
    LOG(ERROR) << "ReportId serialization failed: "
               << ReportStore::ToString(report_id);
    return grpc::Status(grpc::ABORTED, "Unable to build report_id string");
  }
  if (!Base64Encode(serialized_id, id_string_out)) {
    // Note(rudominer) This is just for completeness. I expect this to never
    // happen.
    LOG(ERROR) << "Base64Encode failed: " << ReportStore::ToString(report_id);
    return grpc::Status(grpc::ABORTED, "Unable to build report_id string");
  }
  return grpc::Status::OK;
}

// Builds the ReportId message used in the internal ReportStore API from the
// string form of a report_id used in the public ReportMaster API.
grpc::Status ReportIdFromString(const std::string& id_string,
                                ReportId* report_id_out) {
  std::string serialized_id;
  if (!Base64Decode(id_string, &serialized_id)) {
    LOG(ERROR) << "Base64Encode failed: " << id_string;
    return grpc::Status(grpc::INVALID_ARGUMENT, "Bad report_id.");
  }
  if (!report_id_out->ParseFromString(serialized_id)) {
    LOG(ERROR) << "ParseFromString failed: " << id_string;
    return grpc::Status(grpc::INVALID_ARGUMENT, "Bad report_id.");
  }
  return grpc::Status::OK;
}

// Builds a ReportMetadata to be returned to a client of the public
// ReportMasterAPI, extracting data from the arguments. The |metadata_lite|
// argument will be modified as some data will be swapped out of it.
// Returns OK on success or an error status.
grpc::Status MakeReportMetadata(const std::string& report_id_string,
                                const ReportId& report_id,
                                const ReportConfig* report_config,
                                ReportMetadataLite* metadata_lite,
                                ReportMetadata* metadata) {
  metadata->set_report_id(report_id_string);
  metadata->set_customer_id(report_id.customer_id());
  metadata->set_project_id(report_id.project_id());
  metadata->set_report_config_id(report_id.report_config_id());
  metadata->set_state(metadata_lite->state());
  metadata->mutable_creation_time()->set_seconds(
      report_id.creation_time_seconds());

  // Copy the start_time and finish_time as appropriate.
  switch (metadata->state()) {
    case WAITING_TO_START:
      break;

    case IN_PROGRESS:
      metadata->mutable_start_time()->set_seconds(
          metadata_lite->start_time_seconds());
      break;

    case COMPLETED_SUCCESSFULLY:
    case TERMINATED:
      metadata->mutable_start_time()->set_seconds(
          metadata_lite->start_time_seconds());
      metadata->mutable_finish_time()->set_seconds(
          metadata_lite->finish_time_seconds());
      break;

    default: {
      std::ostringstream stream;
      stream << "Bad metadata found for report_id="
             << ReportStore::ToString(report_id)
             << ". Unrecognized state: " << metadata->state();
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::FAILED_PRECONDITION, message);
    }
  }

  metadata->set_first_day_index(metadata_lite->first_day_index());
  metadata->set_last_day_index(metadata_lite->last_day_index());

  // Set the metric_parts
  bool error_need_two_metric_parts = false;
  switch (report_id.variable_slice()) {
    case VARIABLE_1:
      metadata->add_metric_parts(report_config->variable(0).metric_part());
      break;
    case VARIABLE_2:
      if (report_config->variable_size() == 2) {
        metadata->add_metric_parts(report_config->variable(1).metric_part());
      } else {
        error_need_two_metric_parts = true;
      }
      break;
    case JOINT:
      if (report_config->variable_size() == 2) {
        metadata->add_metric_parts(report_config->variable(0).metric_part());
        metadata->add_metric_parts(report_config->variable(1).metric_part());
      } else {
        error_need_two_metric_parts = true;
      }
      break;
    default:
      CHECK(false) << "Bad variable_slice " << report_id.variable_slice();
  }

  if (error_need_two_metric_parts) {
    std::ostringstream stream;
    stream << "Bad report_id=" << ReportStore::ToString(report_id)
           << ". ReportConfig does not have two metric_parts.";
    std::string message = stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::FAILED_PRECONDITION, message);
  }

  // Add the associated_report_ids if this is a joint report.
  if (report_id.variable_slice() == JOINT) {
    ReportId associated_id = report_id;
    // Make the ID for the marginal report for variable 1.
    associated_id.set_variable_slice(VARIABLE_1);
    auto status =
        ReportIdToString(associated_id, metadata->add_associated_report_ids());
    if (!status.ok()) {
      // This is just for completeness. We don't expect it to ever happen.
      LOG(ERROR) << "ReportIdToString failed unexpectedly.";
      return status;
    }

    // Make the ID for the marginal report for variable 2.
    associated_id.set_variable_slice(VARIABLE_2);
    status =
        ReportIdToString(associated_id, metadata->add_associated_report_ids());
    if (!status.ok()) {
      // This is just for completeness. We don't expect it to ever happen.
      LOG(ERROR) << "ReportIdToString failed unexpectedly.";
      return status;
    }
  }

  metadata->set_one_off(metadata_lite->one_off());
  metadata->mutable_info_messages()->Swap(
      metadata_lite->mutable_info_messages());

  return grpc::Status::OK;
}

}  // namespace

std::unique_ptr<ReportMasterService>
ReportMasterService::CreateFromFlagsOrDie() {
  std::shared_ptr<DataStore> data_store(
      DataStore::CreateFromFlagsOrDie().release());
  std::shared_ptr<ObservationStore> observation_store(
      new ObservationStore(data_store));
  std::shared_ptr<ReportStore> report_store(new ReportStore(data_store));

  std::shared_ptr<AnalyzerConfig> analyzer_config(
      AnalyzerConfig::CreateFromFlagsOrDie().release());

  CHECK(FLAGS_port) << "--port is a mandatory flag";

  std::shared_ptr<grpc::ServerCredentials> server_credentials;
  if (FLAGS_ssl_cert_info.empty()) {
    LOG(WARNING) << "WARNING: Using insecure server credentials. Pass "
                    "-ssl_cert_info to enable SSL.";
    server_credentials = grpc::InsecureServerCredentials();
  } else {
    LOG(INFO) << "Reading SSL certificate information from '"
              << FLAGS_ssl_cert_info << "'.";
    grpc::SslServerCredentialsOptions options;
    // TODO(rudominer) Set up options based on FLAGS_ssl_cert_info.
    server_credentials = grpc::SslServerCredentials(options);
  }

  return std::unique_ptr<ReportMasterService>(
      new ReportMasterService(FLAGS_port, observation_store, report_store,
                              analyzer_config, server_credentials));
}

ReportMasterService::ReportMasterService(
    int port, std::shared_ptr<store::ObservationStore> observation_store,
    std::shared_ptr<store::ReportStore> report_store,
    std::shared_ptr<config::AnalyzerConfig> analyzer_config,
    std::shared_ptr<grpc::ServerCredentials> server_credentials)
    : port_(port),
      observation_store_(observation_store),
      report_store_(report_store),
      analyzer_config_(analyzer_config),
      report_executor_(new ReportExecutor(
          report_store_,
          std::unique_ptr<ReportGenerator>(new ReportGenerator(
              analyzer_config_, observation_store_, report_store_)))),
      server_credentials_(server_credentials) {}

void ReportMasterService::Start() {
  // Start the ReportExecutor worker thread.
  StartWorkerThread();

  grpc::ServerBuilder builder;
  char local_address[1024];
  // We use 0.0.0.0 to indicate the wildcard interface.
  snprintf(local_address, sizeof(local_address), "0.0.0.0:%d", port_);
  builder.AddListeningPort(local_address, server_credentials_);
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  LOG(INFO) << "Starting ReportMaster service on port " << port_;
}

void ReportMasterService::Shutdown() {
  // TODO(rudominer) Stop accepting further requests during shutdown.

  // Wait until all current report generation finishes.
  WaitUntilIdle();

  // Stop the ReportExecutor worker thread.
  report_executor_.reset();

  server_->Shutdown();
}

void ReportMasterService::Wait() { server_->Wait(); }

grpc::Status ReportMasterService::StartReport(ServerContext* context,
                                              const StartReportRequest* request,
                                              StartReportResponse* response) {
  CHECK(request);
  CHECK(response);
  response->Clear();
  uint32_t customer_id = request->customer_id();
  uint32_t project_id = request->project_id();
  uint32_t report_config_id = request->report_config_id();

  // Fetch the ReportConfig from the registry and validate it.
  const ReportConfig* report_config;
  auto status = GetAndValidateReportConfig(customer_id, project_id,
                                           report_config_id, &report_config);
  if (!status.ok()) {
    return status;
  }

  // Set up the fields of the ReportId.
  ReportId report_id;
  report_id.set_customer_id(customer_id);
  report_id.set_project_id(project_id);
  report_id.set_report_config_id(report_config_id);

  // Register the report for variable one as being in the IN_PROGRESS state.
  report_id.set_variable_slice(VARIABLE_1);
  status = StartNewReport(*request, &report_id);
  if (!status.ok()) {
    return status;
  }

  // If there is one variable we generate one report. If there are two variables
  // we generate three reports.
  size_t num_reports = (report_config->variable_size() == 1 ? 1 : 3);
  std::vector<ReportId> report_chain(num_reports);

  // The report for variable one will be generated first.
  report_chain[0] = report_id;

  if (num_reports == 3) {
    // Register the report for variable two as being in the WAITING_TO_START
    // state. Note that this updates |report_id| to refer to VARIABLE_2.
    status = CreateSecondarySlice(VARIABLE_2, &report_id);
    if (!status.ok()) {
      return status;
    }
    // The report for variable 2 will be generated second.
    report_chain[1] = report_id;

    // Register the joint report as being in the WAITING_TO_START state.
    // Note this updates |report_id| to refer to the JOINT report.
    status = CreateSecondarySlice(JOINT, &report_id);
    if (!status.ok()) {
      return status;
    }
    // The joint report will be generated third.
    report_chain[2] = report_id;
  }

  // Build the public report_id string to return in the repsonse. In the case
  // that there are two variables and three reports we now return the report_id
  // of the joint report as this is the primary report the user is interested
  // in. He can learn the IDs of the marginal reports by invoking GetReport()
  // on the primary report and inspecting the |associated_report_ids| in the
  // ReportMetadata in that response.
  status = ReportIdToString(report_id, response->mutable_report_id());
  if (!status.ok()) {
    return status;
  }

  // Finally enqueue the chain of reports to be generated.
  return report_executor_->EnqueueReportGeneration(report_chain);
}

grpc::Status ReportMasterService::GetReport(ServerContext* context,
                                            const GetReportRequest* request,
                                            Report* response) {
  CHECK(request);
  CHECK(response);
  response->Clear();
  // Parse the report_id.
  ReportId report_id;
  auto status = ReportIdFromString(request->report_id(), &report_id);
  if (!status.ok()) {
    return status;
  }

  // Fetch the metadata and possibly the rows from the ReportStore.
  ReportMetadataLite metadata_lite;
  ReportRows report_rows;
  status = GetReport(report_id, &metadata_lite, &report_rows);
  if (!status.ok()) {
    return status;
  }

  // Fetch the ReportConfig from the registry and validate it.
  const ReportConfig* report_config;
  status = GetAndValidateReportConfig(
      report_id.customer_id(), report_id.project_id(),
      report_id.report_config_id(), &report_config);
  if (!status.ok()) {
    return status;
  }

  // Build the ReportMetadata in the response.
  auto metadata = response->mutable_metadata();
  status = MakeReportMetadata(request->report_id(), report_id, report_config,
                              &metadata_lite, metadata);
  if (!status.ok()) {
    return status;
  }

  // Swap over the actual report rows if the report completed successfully.
  if (metadata->state() == COMPLETED_SUCCESSFULLY) {
    response->mutable_rows()->Swap(&report_rows);
  }

  return grpc::Status::OK;
}

grpc::Status ReportMasterService::QueryReports(
    ServerContext* context, const QueryReportsRequest* request,
    ServerWriter<QueryReportsResponse>* writer) {
  return QueryReportsInternal(context, request, writer);
}

grpc::Status ReportMasterService::QueryReportsInternal(
    ServerContext* context, const QueryReportsRequest* request,
    grpc::WriterInterface<QueryReportsResponse>* writer) {
  CHECK(request);
  CHECK(writer);
  // The max number of ReportMetadata we send back in each QueryReportsResponse.
  static const size_t kBatchSize = 100;

  // Extract the fields of the request.
  uint32_t customer_id = request->customer_id();
  uint32_t project_id = request->project_id();
  uint32_t report_config_id = request->report_config_id();
  int64_t interval_start_time_seconds = request->first_timestamp().seconds();
  int64_t interval_limit_time_seconds = request->limit_timestamp().seconds();
  if (request->limit_timestamp().nanos() > 0) {
    interval_limit_time_seconds++;
  }

  // Query the store and return the results in batches of size kBatchSize.
  ReportStore::QueryReportsResponse store_response;
  do {
    // Query one batch from the store, passing in the pagination_token from
    // the previous time through this loop.
    store_response = report_store_->QueryReports(
        customer_id, project_id, report_config_id, interval_start_time_seconds,
        interval_limit_time_seconds, kBatchSize,
        store_response.pagination_token);
    if (store_response.status != store::kOK) {
      LOG(ERROR) << "Read failed during QueryReports.";
      return grpc::Status(grpc::ABORTED, "Read failed.");
    }

    // Iterate through the batch, building up |rpc_response|.
    QueryReportsResponse rpc_response;
    for (auto& store_result : store_response.results) {
      // Build the public report_id string.
      std::string public_report_id_string;
      auto status =
          ReportIdToString(store_result.report_id, &public_report_id_string);
      if (!status.ok()) {
        return status;
      }

      // Fetch the ReportConfig from the registry and validate it.
      const ReportConfig* report_config;
      status = GetAndValidateReportConfig(customer_id, project_id,
                                          report_config_id, &report_config);
      if (!status.ok()) {
        return status;
      }

      // Build the ReportMetadata in the response.
      status = MakeReportMetadata(
          public_report_id_string, store_result.report_id, report_config,
          &store_result.report_metadata, rpc_response.add_reports());
      if (!status.ok()) {
        return status;
      }
    }

    // Send |rpc_response| containing the current batch back to the client.
    if (!writer->Write(rpc_response)) {
      LOG(ERROR) << "Stream closed while writing response from QueryReports.";
      return grpc::Status(grpc::ABORTED, "Stream closed.");
    }
  } while (!store_response.pagination_token.empty());

  return grpc::Status::OK;
}

/////////// private methods ////////////////

grpc::Status ReportMasterService::GetAndValidateReportConfig(
    uint32_t customer_id, uint32_t project_id, uint32_t report_config_id,
    const ReportConfig** report_config_out) {
  // Fetch the ReportConfig from the registry.
  *report_config_out =
      analyzer_config_->ReportConfig(customer_id, project_id, report_config_id);
  if (!*report_config_out) {
    std::ostringstream stream;
    stream << "No ReportConfig found with id=(" << customer_id << ", "
           << project_id << ", " << report_config_id << ")";
    std::string message = stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::NOT_FOUND, message);
  }

  // Make sure it has either one or two variables.
  size_t num_variables = (*report_config_out)->variable_size();
  if (num_variables == 0 || num_variables > 2) {
    std::ostringstream stream;
    stream << "The ReportConfig with id=(" << customer_id << ", " << project_id
           << ", " << report_config_id
           << ") is invalid. Number of variables=" << num_variables
           << ". Cobalt ReportConfigs may have either one or two variables.";
    std::string message = stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::FAILED_PRECONDITION, message);
  }

  return grpc::Status::OK;
}

grpc::Status ReportMasterService::StartNewReport(
    const StartReportRequest& request, ReportId* report_id) {
  // Invoke ReportStore::StartNewRequest().
  auto store_status = report_store_->StartNewReport(
      request.first_day_index(), request.last_day_index(), true, report_id);

  // Log(ERROR) if not OK.
  if (store_status != store::kOK) {
    std::ostringstream stream;
    stream << "StartNewReport failed with status=" << store_status
           << " for report_id=" << ReportStore::ToString(*report_id);
    std::string message = stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::ABORTED, message);
  }
  return grpc::Status::OK;
}

grpc::Status ReportMasterService::CreateSecondarySlice(
    VariableSlice variable_slice, ReportId* report_id) {
  // Invoke ReportStore::CreateSecondarySlice().
  auto store_status =
      report_store_->CreateSecondarySlice(variable_slice, report_id);

  // LOG(ERROR) if not OK.
  if (store_status != store::kOK) {
    std::ostringstream stream;
    stream << "CreateSecondarySlice failed with status=" << store_status
           << " for report_id=" << ReportStore::ToString(*report_id);
    std::string message = stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::ABORTED, message);
  }
  return grpc::Status::OK;
}

grpc::Status ReportMasterService::GetReport(const ReportId& report_id,
                                            ReportMetadataLite* metadata_out,
                                            ReportRows* report_out) {
  // Invoke ReportStore::GetMetadata
  auto store_status =
      report_store_->GetReport(report_id, metadata_out, report_out);

  // LOG(ERROR) if not OK.
  if (store_status != store::kOK) {
    std::ostringstream stream;
    stream << "GetReport failed with status=" << store_status
           << " for report_id=" << ReportStore::ToString(report_id);
    std::string message = stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::ABORTED, message);
  }
  return grpc::Status::OK;
}

void ReportMasterService::StartWorkerThread() { report_executor_->Start(); }

void ReportMasterService::WaitUntilIdle() { report_executor_->WaitUntilIdle(); }

}  // namespace analyzer
}  // namespace cobalt
