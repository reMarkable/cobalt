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

#ifndef COBALT_ANALYZER_STORE_BIGTABLE_ADMIN_H_
#define COBALT_ANALYZER_STORE_BIGTABLE_ADMIN_H_

#include <chrono>
#include <memory>
#include <string>

#include "google/bigtable/admin/v2/bigtable_table_admin.grpc.pb.h"
#include "google/bigtable/v2/data.pb.h"
#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {
namespace store {

// BigtableAdmin is used to create the Cobalt Bigtable tables. This is not
// used in the normal operation of Cobalt. It is used when testing with the
// Bigtable Emulator and it may also be used to build a tool for provisioning a
// data center.
class BigtableAdmin {
 public:
  BigtableAdmin(std::string uri,
                std::shared_ptr<grpc::ChannelCredentials> credentials,
                std::string project_name, std::string instance_name);

  // Wait until deadline to be connected to the the server.
  // Returns whether or not the connection succeeded.
  bool WaitForConnected(std::chrono::system_clock::time_point deadline);

  // Creates the Cobalt tables. Returns true for success.
  bool CreateTablesIfNecessary();

 private:
  bool CreateTableIfNecessary(std::string table_id);

  std::shared_ptr<grpc::Channel> channel_;
  std::shared_ptr<google::bigtable::admin::v2::BigtableTableAdmin::Stub> stub_;
  std::string project_name_;
  std::string instance_name_;
};

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_BIGTABLE_ADMIN_H_
