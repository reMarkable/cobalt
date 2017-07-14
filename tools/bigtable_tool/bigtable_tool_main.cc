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

#include <memory>
#include <string>

#include "analyzer/store/bigtable_admin.h"
#include "analyzer/store/bigtable_store.h"
#include "analyzer/store/data_store.h"
#include "analyzer/store/observation_store.h"
#include "analyzer/store/report_store.h"
#include "gflags/gflags.h"
#include "glog/logging.h"

using cobalt::analyzer::store::BigtableAdmin;
using cobalt::analyzer::store::BigtableStore;
using cobalt::analyzer::store::DataStore;
using cobalt::analyzer::store::ObservationStore;
using cobalt::analyzer::store::ReportStore;
using cobalt::analyzer::store::kOK;

DEFINE_string(
    command, "create_tables",
    "Specify which command to execute. See program help for details.");

DEFINE_uint32(customer, 0, "Customer ID. Used for delete operations.");
DEFINE_uint32(
    project, 0,
    // This tool is not intended to be used to delete real customer data so
    // we do not permit project IDs >= 100.
    "Project ID. Used for delete operations. Must be in the range [0, 99]");
DEFINE_uint32(metric, 0, "Which metric to use for delete_observations.");
DEFINE_uint32(report_config, 0,
              "Which report config to use for delete_reports.");

bool CreateTablesIfNecessary() {
  auto bigtable_admin = BigtableAdmin::CreateFromFlagsOrDie();
  return bigtable_admin->CreateTablesIfNecessary();
}

bool DeleteObservationsForMetric(uint32_t customer_id, uint32_t project_id,
                                 uint32_t metric_id) {
  ObservationStore observation_store(BigtableStore::CreateFromFlagsOrDie());
  return kOK ==
         observation_store.DeleteAllForMetric(customer_id, project_id,
                                              metric_id);
}

bool DeleteReportsForConfig(uint32_t customer_id, uint32_t project_id,
                            uint32_t report_config_id) {
  ReportStore report_store(BigtableStore::CreateFromFlagsOrDie());
  return kOK ==
         report_store.DeleteAllForReportConfig(customer_id, project_id,
                                               report_config_id);
}

int main(int argc, char* argv[]) {
  google::SetUsageMessage(
      "A tool to facilitate working with Cobalt's BigTables in production.\n"
      "usage:\n"
      "bigtable_tool -command=<command> -bigtable_project_name=<name> "
      "-bigtable_instance_name=<name>\n [-customer=<customer_id> "
      "-project=<project_id> "
      "{-metric=<metric_id>, -report_config=<report_config_id>}]\n"
      "commands are:\n"
      "create_tables (the default): Creates Cobalt's tables if they don't "
      "already exist.\n"
      "delete_observations: Permanently delete all data from the "
      "Observation Store for the specified metric.\n"
      "delete_reports: Permanently delete all data from the Report Store "
      "for the specified report config.\n");
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  if (FLAGS_command == "create_tables") {
    if (CreateTablesIfNecessary()) {
      std::cout << "create_tables command succeeded.\n";
    } else {
      std::cout << "create_tables command failed.\n";
    }
  } else if (FLAGS_command == "delete_observations") {
    if (FLAGS_customer * FLAGS_project * FLAGS_metric == 0) {
      std::cout << "Invalid Flags: "
                   "-customer -project -metric must all be specified.\n";
      exit(1);
    }
    if (FLAGS_project >= 100) {
      std::cout << "-project=" << FLAGS_project << " is not allowed. ";
      std::cout << "Project ID must be less than 100.\n";
      std::cout << "This tool is not intended to be used to delete real "
                   "customer data.\n";
      exit(1);
    }
    if (DeleteObservationsForMetric(FLAGS_customer, FLAGS_project,
                                    FLAGS_metric)) {
      std::cout << "delete_observations command succeeded.\n";
    } else {
      std::cout << "delete_observations command failed.\n";
    }
  } else if (FLAGS_command == "delete_reports") {
    if (FLAGS_customer * FLAGS_project * FLAGS_report_config == 0) {
      std::cout << "Invalid flags: "
                   "-customer -project -report_config must all be specified.\n";
      exit(1);
    }
    if (FLAGS_project >= 100) {
      std::cout << "-project=" << FLAGS_project << " is not allowed. ";
      std::cout << "Project ID must be less than 100.\n";
      std::cout << "This tool is not intended to be used to delete real "
                   "customer data.\n";
      exit(1);
    }
    if (DeleteReportsForConfig(FLAGS_customer, FLAGS_project,
                               FLAGS_report_config)) {
      std::cout << "delete_reports command succeeded.\n";
    } else {
      std::cout << "delete_report command failed.\n";
    }
  } else {
    std::cout << "unrecognized command " << FLAGS_command << std::endl;
  }

  exit(0);
}
