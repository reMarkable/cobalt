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

#include "analyzer/report_master/report_generator.h"
#include "analyzer/store/data_store.h"
#include "config/analyzer_config.h"
#include "glog/logging.h"

namespace cobalt {
namespace analyzer {

using config::AnalyzerConfig;
using grpc::ServerWriter;
using store::DataStore;
using store::ObservationStore;
using store::ReportStore;

DEFINE_int32(port, 0,
             "The port that the ReportMaster Service should listen on.");
DEFINE_string(ssl_cert_info, "", "TBD: Some info about SSL Certificates.");

std::unique_ptr<ReportMasterService>
ReportMasterService::CreateFromFlagsOrDie() {
  std::shared_ptr<DataStore> data_store(
      DataStore::CreateFromFlagsOrDie().release());

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

  return std::unique_ptr<ReportMasterService>(new ReportMasterService(
      FLAGS_port, data_store, analyzer_config, server_credentials));
}

ReportMasterService::ReportMasterService(
    int port, std::shared_ptr<store::DataStore> store,
    std::shared_ptr<config::AnalyzerConfig> analyzer_config,
    std::shared_ptr<grpc::ServerCredentials> server_credentials)
    : port_(port),
      observation_store_(new ObservationStore(store)),
      report_store_(new ReportStore(store)),
      analyzer_config_(analyzer_config),
      server_credentials_(server_credentials) {}

void ReportMasterService::Start() {
  grpc::ServerBuilder builder;
  char local_address[1024];
  // We use 0.0.0.0 to indicate the wildcard interface.
  snprintf(local_address, sizeof(local_address), "0.0.0.0:%d", port_);
  builder.AddListeningPort(local_address, server_credentials_);
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  LOG(INFO) << "Starting ReportMaster service on port " << port_;
}

void ReportMasterService::Shutdown() { server_->Shutdown(); }

void ReportMasterService::Wait() { server_->Wait(); }

grpc::Status ReportMasterService::StartReport(grpc::ServerContext* context,
                                              const StartReportRequest* request,
                                              StartReportResponse* response) {
  return grpc::Status::OK;
}

grpc::Status ReportMasterService::GetReport(grpc::ServerContext* context,
                                            const GetReportRequest* request,
                                            Report* response) {
  return grpc::Status::OK;
}

grpc::Status ReportMasterService::QueryReports(
    grpc::ServerContext* context, const QueryReportsRequest* request,
    grpc::ServerWriter<QueryReportsResponse>* writer) {
  return grpc::Status::OK;
}

}  // namespace analyzer
}  // namespace cobalt
