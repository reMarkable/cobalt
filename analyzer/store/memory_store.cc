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

#include "analyzer/store/memory_store.h"

#include <glog/logging.h>

#include <set>

namespace cobalt {
namespace analyzer {
namespace store {

typedef std::map<std::string, std::map<std::string, std::string>> ImplMapType;

MemoryStoreSingleton& MemoryStoreSingleton::Instance() {
  static MemoryStoreSingleton singleton;

  return singleton;
}

Status MemoryStoreSingleton::WriteRow(Table table, Row row) {
  ImplMapType& rows =
      (table == kObservations ? observation_rows_ : report_rows_);
  rows[row.key].clear();
  rows[row.key] = std::move(row.column_values);
  return kOK;
}

DataStore::ReadResponse MemoryStoreSingleton::ReadRows(
    Table table, std::string start_row_key, bool inclusive,
    std::string limit_row_key, std::vector<std::string> column_names,
    size_t max_rows) {
  if (max_rows == 0 || max_rows > 100) {
    max_rows = 100;
  }

  ImplMapType& rows =
      (table == kObservations ? observation_rows_ : report_rows_);

  // Find the first row of the range (inclusive or exclusive)
  ImplMapType::iterator start_iterator;
  if (inclusive) {
    start_iterator = rows.lower_bound(start_row_key);
  } else {
    start_iterator = rows.upper_bound(start_row_key);
  }

  ImplMapType::iterator limit_iterator;
  if (limit_row_key.empty()) {
    limit_iterator = rows.end();
  } else {
    // Find the least row greater than or equal to limit_row_key.
    limit_iterator = rows.lower_bound(limit_row_key);
  }

  ReadResponse read_response;
  read_response.more_available = false;

  // Make a set of the requested column_names
  std::set<std::string> requested_column_names(column_names.begin(),
                                               column_names.end());

  // Iterate through the rows of the range.
  for (ImplMapType::iterator row_iterator = start_iterator;
       row_iterator != limit_iterator; row_iterator++) {
    if (read_response.rows.size() == max_rows) {
      read_response.more_available = true;
      break;
    }
    // For each row add a row to read_response.
    read_response.rows.emplace_back();
    // Our map key is the row key.
    read_response.rows.back().key = row_iterator->first;
    // Our map value is a map of column-name to column-value.
    auto& column_values = read_response.rows.back().column_values;
    // Iterate through this sub-map.
    for (const auto& pair : row_iterator->second) {
      // For each element of the sub-map add a ColumnValue to column_values.
      if (requested_column_names.empty() ||
          requested_column_names.find(pair.first) !=
              requested_column_names.end()) {
        column_values[pair.first] = pair.second;
      }
    }
  }

  read_response.status = kOK;
  return read_response;
}

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
