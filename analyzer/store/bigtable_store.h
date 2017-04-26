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
  static std::unique_ptr<BigtableStore> CreateFromFlagsOrDie();

  BigtableStore(const std::string& uri, const std::string& admin_uri,
                std::shared_ptr<grpc::ChannelCredentials> credentials,
                const std::string& project_name,
                const std::string& instance_name);

  Status WriteRow(Table table, DataStore::Row row) override;

  Status WriteRows(Table table, std::vector<Row> rows) override;

  Status ReadRow(Table table, const std::vector<std::string>& column_names,
                 Row* row) override;

  ReadResponse ReadRows(Table table, std::string start_row_key, bool inclusive,
                        std::string limit_row_key,
                        const std::vector<std::string>& column_names,
                        size_t max_rows) override;

  Status DeleteRow(Table table, std::string row_key) override;

  Status DeleteRowsWithPrefix(Table table, std::string row_key_prefix) override;

  Status DeleteAllRows(Table table) override;

 private:
  std::string TableName(DataStore::Table table);

  // DoWriteRows does the work of WriteRows(). WriteRows() invokes DoWriteRows()
  // in a loop, retrying with exponential backoff when a retryable error occurs.
  grpc::Status DoWriteRows(Table table, const std::vector<Row>& rows);

  // This method is used to implement ReadRow and ReadRows. It is identical to
  // ReadRows except that instead of limit_row_key it has end_row_key and
  // inclusive_end. In other words it supports intervals that are closed on
  // the right.
  ReadResponse ReadRowsInternal(Table table, std::string start_row_key,
                                bool inclusive_start, std::string end_row_key,
                                bool inclusive_end,
                                const std::vector<std::string>& column_names,
                                size_t max_rows);

  std::unique_ptr<google::bigtable::v2::Bigtable::Stub> stub_;
  std::unique_ptr<google::bigtable::admin::v2::BigtableTableAdmin::Stub>
      admin_stub_;
  std::string observations_table_name_, report_progress_table_name_,
      report_rows_table_name_;
};

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_BIGTABLE_STORE_H_
