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

#ifndef COBALT_ANALYZER_STORE_BIGTABLE_STORE_H_
#define COBALT_ANALYZER_STORE_BIGTABLE_STORE_H_

#include <google/bigtable/v2/bigtable.grpc.pb.h>
#include <google/bigtable/admin/v2/bigtable_table_admin.grpc.pb.h>
#include <grpc++/grpc++.h>

#include <map>
#include <memory>
#include <string>

#include "analyzer/store/store.h"

namespace cobalt {
namespace analyzer {

// A key value store implemented on Google Cloud Bigtable
class BigtableStore : public Store {
 public:
  // table_name format:
  //   "projects/PROJECT_NAME/instances/INSTANCE_NAME/tables/TABLE_NAME"
  explicit BigtableStore(const std::string& table_name);

  // Call prior to put/get.  Sets up needed state for connecting to bigtable.
  // init_schema: whether or not to create tables.
  // Returns non-zero on error.
  int initialize(bool init_schema);

  int put(const std::string& key, const std::string& val) override;
  int get(const std::string& key, std::string* out) override;

  int get_range(const std::string& start, const std::string& end,
                std::map<std::string, std::string>* out) override;

 private:
  int setup_connection();
  int init_schema();

  std::unique_ptr<google::bigtable::v2::Bigtable::Stub> data_;
  std::unique_ptr<google::bigtable::admin::v2::BigtableTableAdmin::Stub> admin_;
  std::string table_name_;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_BIGTABLE_STORE_H_
