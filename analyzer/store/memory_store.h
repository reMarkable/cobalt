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

  ReadResponse ReadRows(Table table, std::string start_row_key, bool inclusive,
                        std::string limit_row_key,
                        std::vector<std::string> columns,
                        size_t max_rows) override;

  // Deletes all data from the store.
  void Clear() {
    observation_rows_.clear();
    report_rows_.clear();
  }

 private:
  MemoryStoreSingleton() {}

  std::map<std::string, std::map<std::string, std::string>> observation_rows_,
      report_rows_;
};

// An in-memory implementation of DataStore. The backing store is a singleton
// shared by all MemoryStore instances.
class MemoryStore : public DataStore {
 public:
  Status WriteRow(Table table, Row row) override {
    return MemoryStoreSingleton::Instance().WriteRow(table, std::move(row));
  }

  ReadResponse ReadRows(Table table, std::string start_row_key, bool inclusive,
                        std::string limit_row_key,
                        std::vector<std::string> columns,
                        size_t max_rows) override {
    return MemoryStoreSingleton::Instance().ReadRows(
        table, start_row_key, inclusive, limit_row_key, columns, max_rows);
  }

  // Deletes all data from the store.
  void Clear() { MemoryStoreSingleton::Instance().Clear(); }
};

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_MEMORY_STORE_H_
