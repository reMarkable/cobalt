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

#include "analyzer/report_master/report_master_service_abstract_test.h"
#include "analyzer/store/memory_store_test_helper.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {

// Instantiate ReportMasterServiceAbstractTest using the MemoryStore as the
// underlying DataStore.
INSTANTIATE_TYPED_TEST_CASE_P(ReportMasterServiceTest,
                              ReportMasterServiceAbstractTest,
                              store::MemoryStoreFactory);

// Checks that permissions are checked on all methods of ReportMasterService.
TEST(ReportMasterServiceFriendTest, AuthEnforcerTest) {
  std::shared_ptr<store::ObservationStore> observation_store;
  std::shared_ptr<store::ReportStore> report_store;
  std::shared_ptr<config::AnalyzerConfig> analyzer_config;
  std::shared_ptr<grpc::ServerCredentials> server_credentials;
  std::shared_ptr<AuthEnforcer> auth_enforcer(new NegativeEnforcer());

  ReportMasterService service(0, observation_store, report_store,
                              analyzer_config, server_credentials,
                              auth_enforcer);

  StartReportRequest start_request;
  StartReportResponse start_response;
  EXPECT_EQ(grpc::StatusCode::PERMISSION_DENIED,
            service.StartReport(nullptr, &start_request, &start_response)
                .error_code());

  GetReportRequest get_request;
  Report get_response;
  EXPECT_EQ(
      grpc::StatusCode::PERMISSION_DENIED,
      service.GetReport(nullptr, &get_request, &get_response).error_code());

  QueryReportsRequest query_request;
  TestingQueryReportsResponseWriter query_response;
  EXPECT_EQ(
      grpc::StatusCode::PERMISSION_DENIED,
      service.QueryReportsInternal(nullptr, &query_request, &query_response)
          .error_code());
}

}  // namespace analyzer
}  // namespace cobalt
