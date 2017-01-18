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

#ifndef COBALT_ANALYZER_STORE_BIGTABLE_STORE_H_
#define COBALT_ANALYZER_STORE_BIGTABLE_STORE_H_

#include <google/bigtable/admin/v2/bigtable_table_admin.grpc.pb.h>
#include <google/bigtable/v2/bigtable.grpc.pb.h>
#include <grpc++/grpc++.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "analyzer/store/data_store.h"

namespace cobalt {
namespace analyzer {
namespace store {

// An implementation of DataStore backed by Google Cloud Bigtable
class BigtableStore : public DataStore {
 public:
  // Creates and returns an instance of BigtableStore using the well-known
  // URI of Google Cloud Bigtable, credentials for for the Cobalt service
  // account read from the file named in the environment variable
  // GOOGLE_APPLICATION_CREDENTIALS, and the project and instance
  // names read from flags.
  static std::shared_ptr<BigtableStore> CreateFromFlagsOrDie();

  BigtableStore(std::string uri,
                std::shared_ptr<grpc::ChannelCredentials> credentials,
                std::string project_name, std::string instance_name);

  Status WriteRow(Table table, DataStore::Row row) override;

  ReadResponse ReadRows(Table table, std::string start_row_key, bool inclusive,
                        std::string limit_row_key,
                        const std::vector<std::string>& column_names,
                        size_t max_rows) override;

  Status DeleteRow(Table table, std::string row_key) override;

 private:
  std::string TableName(DataStore::Table table);

  std::unique_ptr<google::bigtable::v2::Bigtable::Stub> stub_;
  std::string observations_table_name_, reports_table_name_;
};

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_BIGTABLE_STORE_H_
