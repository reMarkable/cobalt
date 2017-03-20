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

MemoryStoreSingleton::ImplMapType& MemoryStoreSingleton::GetRows(
    Table which_table) {
  switch (which_table) {
    case kObservations:
      return observation_rows_;
    case kReportMetadata:
      return report_metadata_rows_;
    case kReportRows:
      return report_rows_rows_;
    default:
      CHECK(false) << "Unrecognized table" << which_table;
  }
}

MemoryStoreSingleton& MemoryStoreSingleton::Instance() {
  static MemoryStoreSingleton singleton;

  return singleton;
}

Status MemoryStoreSingleton::WriteRow(Table table, Row row) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto& rows = GetRows(table);
  rows[row.key].clear();
  rows[row.key] = std::move(row.column_values);
  return kOK;
}

Status MemoryStoreSingleton::WriteRows(Table table, std::vector<Row> rows) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  size_t total_num_columns = 0;
  for (Row& row : rows) {
    total_num_columns += row.column_values.size();
    if (total_num_columns > 100000) {
      LOG(ERROR) << "Too much data. Only 100,000 columns total allowed.";
      return kInvalidArguments;
    }
    WriteRow(table, std::move(row));
  }
  return kOK;
}

Status MemoryStoreSingleton::ReadRow(
    Table table, const std::vector<std::string>& column_names, Row* row) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (row == nullptr) {
    return kInvalidArguments;
  }

  auto& rows = GetRows(table);
  auto iter = rows.find(row->key);
  if (iter == rows.end()) {
    VLOG(4) << row->key << " Not found in table " << table;
    return kNotFound;
  }

  // Make a set of the requested column_names
  std::set<std::string> requested_column_names(column_names.begin(),
                                               column_names.end());

  // iter->second is a map of column-name to column-value.
  // Iterate through this sub-map.
  for (const auto& pair : iter->second) {
    // For each element of the sub-map add a ColumnValue to column_values.
    if (requested_column_names.empty() ||
        requested_column_names.find(pair.first) !=
            requested_column_names.end()) {
      row->column_values[pair.first] = pair.second;
    }
  }

  return kOK;
}

DataStore::ReadResponse MemoryStoreSingleton::ReadRows(
    Table table, std::string start_row_key, bool inclusive,
    std::string limit_row_key, const std::vector<std::string>& column_names,
    size_t max_rows) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ReadResponse read_response;
  read_response.status = kOK;
  if (max_rows == 0) {
    read_response.status = kInvalidArguments;
    return read_response;
  }

  auto& rows = GetRows(table);

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

Status MemoryStoreSingleton::DeleteRow(Table table, std::string row_key) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  GetRows(table).erase(row_key);
  return kOK;
}

Status MemoryStoreSingleton::DeleteRowsWithPrefix(Table table,
                                                  std::string row_key_prefix) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (row_key_prefix.empty()) {
    return kInvalidArguments;
  }

  auto& rows = GetRows(table);

  // Find the first row of the range.
  auto start_iterator = rows.lower_bound(row_key_prefix);

  // Find the first row past the range.
  size_t last_byte_index = row_key_prefix.size() - 1;
  row_key_prefix[last_byte_index]++;
  auto limit_iterator = rows.lower_bound(std::move(row_key_prefix));

  rows.erase(start_iterator, limit_iterator);
  return kOK;
}

Status MemoryStoreSingleton::DeleteAllRows(Table table) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  GetRows(table).clear();
  return kOK;
}

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
