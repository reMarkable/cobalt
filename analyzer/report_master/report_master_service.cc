
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
#include "analyzer/store/bigtable_store.h"
#include "analyzer/store/data_store.h"
#include "config/analyzer_config.h"
#include "config/analyzer_config_manager.h"
#include "glog/logging.h"
#include "util/crypto_util/base64.h"
#include "util/pem_util.h"

namespace cobalt {
namespace analyzer {

using config::AnalyzerConfig;
using config::AnalyzerConfigManager;
using crypto::Base64Decode;
using crypto::Base64Encode;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::WriteOptions;
using store::BigtableStore;
using store::DataStore;
using store::ObservationStore;
using store::ReportStore;
using util::PemUtil;

DEFINE_int32(port, 0,
             "The port that the ReportMaster Service should listen on.");
DEFINE_bool(use_tls, false,
            "Should the ReportMaster use TLS for communicating with clients? "
            "Default=false. (Note that in production the ReportMaster is "
            "protected by Google Cloud Endpoints which does use TLS.)");
DEFINE_string(tls_cert_file, "",
              "Path to a TLS server cert file to use if use_tls=true.");
DEFINE_string(tls_key_file, "",
              "Path to a TLS server private key file to use if use_tls=true.");
DEFINE_bool(
    enable_report_scheduling, false,
    "Should the ReportMaster run all reports automatically on a schedule?");

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
// ReportMaster API, extracting data from the arguments. The |metadata_lite|
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
  metadata->set_report_type(metadata_lite->report_type());

  if (metadata_lite->variable_indices_size() == 0) {
    std::ostringstream stream;
    stream << "Invalid metadata, no variable indices for report_id="
           << ReportStore::ToString(report_id);
    std::string message = stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::FAILED_PRECONDITION, message);
  }

  // Set the metric parts.
  for (int index : metadata_lite->variable_indices()) {
    if (index >= report_config->variable_size()) {
      std::ostringstream stream;
      stream << "Invalid variable index encountered while processing report_id="
             << ReportStore::ToString(report_id) << ". index=" << index
             << ". variable_size=" << report_config->variable_size();
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::FAILED_PRECONDITION, message);
    }
    metadata->add_metric_parts(report_config->variable(index).metric_part());
  }

  // Add the associated_report_ids as appropriate. Currently we do this only
  // in the case tht the report type is JOINT. In this case the ReportId's
  // sequence_num should be 2 and we add as associated reports the ReportIDs
  // with sequence_nums 0 and 1 which should be the two one-way marginals.
  if (metadata->report_type() == JOINT) {
    if (report_id.sequence_num() != 2) {
      std::ostringstream stream;
      stream << "Inconsistent metadata encountered while processing report_id="
             << ReportStore::ToString(report_id)
             << ". sequence_num=" << report_id.sequence_num()
             << " but report_type == JOINT.";
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::FAILED_PRECONDITION, message);
    }
    ReportId associated_id = report_id;
    // Make the ID for the marginal report for variable 1.
    associated_id.set_sequence_num(0);
    auto status =
        ReportIdToString(associated_id, metadata->add_associated_report_ids());
    if (!status.ok()) {
      // This is just for completeness. We don't expect it to ever happen.
      LOG(ERROR) << "ReportIdToString failed unexpectedly.";
      return status;
    }

    // Make the ID for the marginal report for variable 2.
    associated_id.set_sequence_num(1);
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
      BigtableStore::CreateFromFlagsOrDie().release());
  std::shared_ptr<ObservationStore> observation_store(
      new ObservationStore(data_store));
  std::shared_ptr<ReportStore> report_store(new ReportStore(data_store));

  std::shared_ptr<AnalyzerConfigManager> config_manager(
      AnalyzerConfigManager::CreateFromFlagsOrDie());

  std::shared_ptr<AuthEnforcer> auth_enforcer =
      AuthEnforcer::CreateFromFlagsOrDie();

  CHECK(FLAGS_port) << "--port is a mandatory flag";

  std::shared_ptr<grpc::ServerCredentials> server_credentials;
  if (FLAGS_use_tls) {
    LOG(INFO) << "Using TLS.";
    std::string tls_server_cert;
    CHECK(PemUtil::ReadTextFile(FLAGS_tls_cert_file, &tls_server_cert))
        << "Error reading tls cert file: " << FLAGS_tls_cert_file;
    LOG(INFO) << "TLS server cert successfully read from  "
              << FLAGS_tls_cert_file;
    std::string tls_server_private_key;
    CHECK(PemUtil::ReadTextFile(FLAGS_tls_key_file, &tls_server_private_key))
        << "Error reading tls server private key file: " << FLAGS_tls_key_file;
    LOG(INFO) << "TLS server private key successfully read from  "
              << FLAGS_tls_key_file;
    grpc::SslServerCredentialsOptions options;
    options.pem_key_cert_pairs.emplace_back();
    options.pem_key_cert_pairs.back().private_key =
        std::move(tls_server_private_key);
    options.pem_key_cert_pairs.back().cert_chain = std::move(tls_server_cert);
    server_credentials = grpc::SslServerCredentials(options);
  } else {
    LOG(WARNING) << "Using insecure server credentials becuase -use_tls=false.";
    server_credentials = grpc::InsecureServerCredentials();
  }

  // We construct a ReportExporter that uses a GcsUploader in order to
  // upload serialized reports to Google Cloud Storage.
  std::shared_ptr<GcsUploader> gcs_uploader(new GcsUploader());
  std::unique_ptr<ReportExporter> report_exporter(
      new ReportExporter(gcs_uploader));

  auto report_master_service =
      std::unique_ptr<ReportMasterService>(new ReportMasterService(
          FLAGS_port, observation_store, report_store, config_manager,
          server_credentials, auth_enforcer, std::move(report_exporter)));

  if (FLAGS_enable_report_scheduling) {
    LOG(INFO) << "Starting a Report Scheduler because "
                 "-enable_report_scheduling=true.";
    // We will construct a new ReportScheduler, giving it a ReportStarter that
    // delegates to our ReportMasterService. The ReportStarter does not take
    // ownership of the ReportMasterService.
    std::shared_ptr<ReportStarter> report_starter(
        new ReportStarter(report_master_service.get()));
    std::unique_ptr<ReportScheduler> report_scheduler(
        new ReportScheduler(config_manager, report_store, report_starter));
    // We start the scheduler thread.
    report_scheduler->Start();
    // We give ownership of the ReportScheduler to the ReportMaster.
    report_master_service->set_report_scheduler(std::move(report_scheduler));
  } else {
    LOG(INFO) << "Not starting a Report Scheduler because "
                 "-enable_report_scheduling=false.";
  }

  return report_master_service;
}

ReportMasterService::ReportMasterService(
    int port, std::shared_ptr<store::ObservationStore> observation_store,
    std::shared_ptr<store::ReportStore> report_store,
    std::shared_ptr<config::AnalyzerConfigManager> config_manager,
    std::shared_ptr<grpc::ServerCredentials> server_credentials,
    std::shared_ptr<AuthEnforcer> auth_enforcer,
    std::unique_ptr<ReportExporter> report_exporter)
    : port_(port),
      observation_store_(observation_store),
      report_store_(report_store),
      config_manager_(config_manager),
      report_executor_(new ReportExecutor(
          report_store_, std::unique_ptr<ReportGenerator>(new ReportGenerator(
                             config_manager_, observation_store_, report_store_,
                             std::move(report_exporter))))),
      server_credentials_(server_credentials),
      auth_enforcer_(auth_enforcer) {}

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
  grpc::Status auth_status = auth_enforcer_->CheckAuthorization(
      context, request->customer_id(), request->project_id(),
      request->report_config_id());
  if (!auth_status.ok()) {
    return auth_status;
  }

  // Since we are starting the report in response to an RPC, this is a
  // one-off report.
  bool one_off = true;
  // We do not export one-off reports to Google Cloud Storage.
  std::string export_name = "";
  // We do store one-off reports in the ReportStore.
  bool in_store = true;
  ReportId report_id_not_used;
  return StartReportNoAuth(request, one_off, export_name, in_store,
                           &report_id_not_used, response);
}

grpc::Status ReportMasterService::StartReportNoAuth(
    const StartReportRequest* request, bool one_off,
    const std::string& export_name, bool in_store, ReportId* report_id_out,
    StartReportResponse* response) {
  CHECK(request);
  CHECK(response);
  CHECK(report_id_out);
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
  report_id_out->Clear();
  report_id_out->set_customer_id(customer_id);
  report_id_out->set_project_id(project_id);
  report_id_out->set_report_config_id(report_config_id);

  switch (report_config->report_type()) {
    case HISTOGRAM:
      return StartHistogramReport(*request, one_off, export_name, in_store,
                                  report_id_out, response);
      break;

    case JOINT:
      return StartJointReport(*request, one_off, export_name, in_store,
                              report_id_out, response);
      break;

    default:
      std::ostringstream stream;
      stream << "Bad ReportConfig found with id=" << report_config_id
             << ". Unrecognized report type: " << report_config->report_type();
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::FAILED_PRECONDITION, message);
  }
}

grpc::Status ReportMasterService::StartHistogramReport(
    const StartReportRequest& request, bool one_off,
    const std::string& export_name, bool in_store, ReportId* report_id,
    StartReportResponse* response) {
  // We will be creating and starting one report only.
  report_id->set_sequence_num(0);
  std::vector<uint32_t> variable_indices = {0};
  auto status = StartNewReport(request, one_off, export_name, in_store,
                               HISTOGRAM, variable_indices, report_id);
  if (!status.ok()) {
    return status;
  }

  // Build the public report_id string to return in the repsonse.
  status = ReportIdToString(*report_id, response->mutable_report_id());
  if (!status.ok()) {
    return status;
  }

  // Finally enqueue the chain of one report to be generated.
  std::vector<ReportId> report_chain(1);
  report_chain[0] = *report_id;
  return report_executor_->EnqueueReportGeneration(report_chain);
}

grpc::Status ReportMasterService::StartJointReport(
    const StartReportRequest& request, bool one_off,
    const std::string& export_name, bool in_store, ReportId* report_id,
    StartReportResponse* response) {
  // We will be creating three reports all together and starting the first one.
  std::vector<ReportId> report_chain(3);

  // First we create and start the HISTOGRAM report for the first marginal.
  report_id->set_sequence_num(0);
  std::vector<uint32_t> variable_indices = {0};  // Specify the first variable.
  // We do not export the marginal reports, so export_name is set to "".
  auto status = StartNewReport(request, one_off, "", in_store, HISTOGRAM,
                               variable_indices, report_id);
  if (!status.ok()) {
    return status;
  }
  report_chain[0] = *report_id;

  // Second we create, but don't yet start, the HISTOGRAM report for the second
  // marginal.
  variable_indices = {1};  // Specify the second variable
  size_t sequence_number = 1;
  // This call will modify report_id to specify the new sequence_number.
  // We do not export the marginal reports, so export_name is set to "".
  status = CreateDependentReport(sequence_number, "", in_store, HISTOGRAM,
                                 variable_indices, report_id);
  if (!status.ok()) {
    return status;
  }
  report_chain[1] = *report_id;

  // Third we create, but don't yet start, the JOINT report.
  variable_indices = {0, 1};  // Specify both variables.
  sequence_number = 2;
  // This call will modify report_id to specify the new sequence_number.
  status = CreateDependentReport(sequence_number, export_name, in_store, JOINT,
                                 variable_indices, report_id);
  if (!status.ok()) {
    return status;
  }
  report_chain[2] = *report_id;

  // Build the public report_id string to return in the repsonse. We return the
  // report_id of the joint report as this is the primary report the user is
  // interested in. He can learn the IDs of the marginal reports by invoking
  // GetReport() on the primary report and inspecting the
  // |associated_report_ids| in the ReportMetadata in that response.
  status = ReportIdToString(*report_id, response->mutable_report_id());
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
  // Parse the report_id.
  ReportId report_id;
  auto status = ReportIdFromString(request->report_id(), &report_id);
  if (!status.ok()) {
    return status;
  }

  grpc::Status auth_status = auth_enforcer_->CheckAuthorization(
      context, report_id.customer_id(), report_id.project_id(),
      report_id.report_config_id());
  if (!auth_status.ok()) {
    return auth_status;
  }

  return GetReportNoAuth(request, response);
}

grpc::Status ReportMasterService::GetReportNoAuth(
    const GetReportRequest* request, Report* response) {
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
  grpc::Status auth_status = auth_enforcer_->CheckAuthorization(
      context, request->customer_id(), request->project_id(),
      request->report_config_id());
  if (!auth_status.ok()) {
    return auth_status;
  }
  return QueryReportsNoAuth(request, writer);
}

grpc::Status ReportMasterService::QueryReportsNoAuth(
    const QueryReportsRequest* request,
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
  auto analyzer_config = config_manager_->GetCurrent();
  // Fetch the ReportConfig from the registry.
  *report_config_out =
      analyzer_config->ReportConfig(customer_id, project_id, report_config_id);
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
    const StartReportRequest& request, bool one_off,
    const std::string& export_name, bool in_store, ReportType report_type,
    const std::vector<uint32_t>& variable_indices, ReportId* report_id) {
  // Invoke ReportStore::StartNewReport().
  auto store_status = report_store_->StartNewReport(
      request.first_day_index(), request.last_day_index(), one_off, export_name,
      in_store, report_type, variable_indices, report_id);

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

grpc::Status ReportMasterService::CreateDependentReport(
    uint32_t sequence_number, const std::string& export_name, bool in_store,
    ReportType report_type, const std::vector<uint32_t>& variable_indices,
    ReportId* report_id) {
  auto store_status = report_store_->CreateDependentReport(
      sequence_number, export_name, in_store, report_type, variable_indices,
      report_id);

  // LOG(ERROR) if not OK.
  if (store_status != store::kOK) {
    std::ostringstream stream;
    stream << "CreateDependentReport failed with status=" << store_status
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

std::string ReportMasterService::MakeStringReportId(const ReportId& report_id) {
  std::string string_id_out;
  ReportIdToString(report_id, &string_id_out);
  return string_id_out;
}

void ReportMasterService::StartWorkerThread() { report_executor_->Start(); }

void ReportMasterService::WaitUntilIdle() { report_executor_->WaitUntilIdle(); }

}  // namespace analyzer
}  // namespace cobalt
