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

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_MASTER_SERVICE_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_MASTER_SERVICE_H_

#include <memory>
#include <string>

#include "analyzer/report_master/report_executor.h"
#include "analyzer/report_master/report_master.grpc.pb.h"
#include "analyzer/store/observation_store.h"
#include "analyzer/store/report_store.h"
#include "config/analyzer_config.h"
#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {

class ReportMasterService final : public ReportMaster::Service {
 public:
  static std::unique_ptr<ReportMasterService> CreateFromFlagsOrDie();

  ReportMasterService(
      int port, std::shared_ptr<store::ObservationStore> observation_store,
      std::shared_ptr<store::ReportStore> report_store,
      std::shared_ptr<config::AnalyzerConfig> analyzer_config,
      std::shared_ptr<grpc::ServerCredentials> server_credentials);

  // Starts the service
  void Start();

  // Stops the service
  void Shutdown();

  // Waits for the service to terminate. Shutdown() must be called for
  // Wait() to return.
  void Wait();

  grpc::Status StartReport(grpc::ServerContext* context,
                           const StartReportRequest* request,
                           StartReportResponse* response) override;

  grpc::Status GetReport(grpc::ServerContext* context,
                         const GetReportRequest* request,
                         Report* response) override;

  grpc::Status QueryReports(
      grpc::ServerContext* context, const QueryReportsRequest* request,
      grpc::ServerWriter<QueryReportsResponse>* writer) override;

 private:
  // Makes all instantiations of ReportMasterServiceAbstractTest friends.
  template <class X>
  friend class ReportMasterServiceAbstractTest;

  // Gets amd validates a ReportConfig. Returns OK or
  // does Log(ERROR) and returns an error status on error.
  grpc::Status GetAndValidateReportConfig(
      uint32_t customer_id, uint32_t project_id, uint32_t report_config_id,
      const ReportConfig** report_config_out);

  // Invokes ReportStore::StartNewReport().
  // Does Log(ERROR) and returns an error status on error.
  grpc::Status StartNewReport(const StartReportRequest& request,
                              ReportId* report_id);

  // Invokes ReportStore::CreateSecondarySlice().
  // Does Log(ERROR) and returns an error status on error.
  grpc::Status CreateSecondarySlice(VariableSlice variable_slice,
                                    ReportId* report_id);

  // Invokes ReportStore::GetReport().
  // Does Log(ERROR) and returns an error status on error.
  grpc::Status GetReport(const ReportId& report_id,
                         ReportMetadataLite* metadata_out,
                         ReportRows* report_out);

  // Starts the worker thread in the ReportExecutor.
  void StartWorkerThread();

  // Blocks until the ReportExecutor is idle. See comments for
  // ReportExecutor::WaitUntilIdle.
  void WaitUntilIdle();

  // gRPC server-side streaming is unmocakble as written so we add a thin
  // mockable wrapper around it so that we can test QueryReports without
  // using the network stack.
  grpc::Status QueryReportsInternal(
      grpc::ServerContext* context, const QueryReportsRequest* request,
      grpc::WriterInterface<QueryReportsResponse>* writer);

  // Returns the string version of a ReportId as used in the gRPC API. This
  // is exposed for use by tests.
  std::string static MakeStringReportId(const ReportId& report_id);

  int port_;
  std::shared_ptr<store::ObservationStore> observation_store_;
  std::shared_ptr<store::ReportStore> report_store_;
  std::shared_ptr<config::AnalyzerConfig> analyzer_config_;
  std::unique_ptr<ReportExecutor> report_executor_;
  std::shared_ptr<grpc::ServerCredentials> server_credentials_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_MASTER_SERVICE_H_
