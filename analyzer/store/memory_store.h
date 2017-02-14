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

#ifndef COBALT_ANALYZER_STORE_MEMORY_STORE_H_
#define COBALT_ANALYZER_STORE_MEMORY_STORE_H_

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/store/data_store.h"

namespace cobalt {
namespace analyzer {
namespace store {

// An in-memory implementation of DataStore.
class MemoryStoreSingleton : public DataStore {
 public:
  static MemoryStoreSingleton& Instance();

  Status WriteRow(Table table, Row row) override;

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
  typedef std::map<std::string, std::map<std::string, std::string>> ImplMapType;

  MemoryStoreSingleton() {}

  ImplMapType& GetRows(Table which_table);

  std::map<std::string, std::map<std::string, std::string>> observation_rows_,
      report_metadata_rows_, report_rows_rows_;

  // protects observation_rows_, report_metadata_rows_, report_rows_rows_.
  std::recursive_mutex mutex_;
};

// An in-memory implementation of DataStore. The backing store is a singleton
// shared by all MemoryStore instances.
class MemoryStore : public DataStore {
 public:
  Status WriteRow(Table table, Row row) override {
    return MemoryStoreSingleton::Instance().WriteRow(table, std::move(row));
  }

  Status WriteRows(Table table, std::vector<Row> rows) override {
    return MemoryStoreSingleton::Instance().WriteRows(table, std::move(rows));
  }

  Status ReadRow(Table table, const std::vector<std::string>& column_names,
                 Row* row) override {
    return MemoryStoreSingleton::Instance().ReadRow(table, column_names, row);
  }

  ReadResponse ReadRows(Table table, std::string start_row_key, bool inclusive,
                        std::string limit_row_key,
                        const std::vector<std::string>& column_names,
                        size_t max_rows) override {
    return MemoryStoreSingleton::Instance().ReadRows(
        table, start_row_key, inclusive, limit_row_key, column_names, max_rows);
  }

  Status DeleteRow(Table table, std::string row_key) override {
    return MemoryStoreSingleton::Instance().DeleteRow(table, row_key);
  }

  Status DeleteRowsWithPrefix(Table table,
                              std::string row_key_prefix) override {
    return MemoryStoreSingleton::Instance().DeleteRowsWithPrefix(
        table, row_key_prefix);
  }

  Status DeleteAllRows(Table table) override {
    return MemoryStoreSingleton::Instance().DeleteAllRows(table);
  }
};

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_MEMORY_STORE_H_
