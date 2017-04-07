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
#include "gflags/gflags.h"
#include "glog/logging.h"

using cobalt::analyzer::store::BigtableAdmin;

DEFINE_string(
    command, "create_tables",
    "Specify which command to execute. See program help for details.");

bool CreateTablesIfNecessary() {
  auto bigtable_admin = BigtableAdmin::CreateFromFlagsOrDie();
  return bigtable_admin->CreateTablesIfNecessary();
}

int main(int argc, char* argv[]) {
  google::SetUsageMessage(
      "A tool to facilitate working with Cobalt's BigTables in production.\n"
      "usage:\n"
      "bigtable_tool --command=<command> --bigtable_project_name=<name> "
      "--bigtable_instance_name=<name>\n"
      "commands are:\n"
      "create_tables (the default): Creates Cobalt's tables if they don't "
      "already exist.");
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  if (FLAGS_command == "create_tables") {
    if (CreateTablesIfNecessary()) {
      std::cout << "create_tables command succeeded.\n";
    } else {
      std::cout << "create_tables command failed.\n";
    }
  } else {
    std::cout << "unrecognized command " << FLAGS_command << std::endl;
  }

  exit(0);
}
