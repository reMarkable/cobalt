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

#ifndef COBALT_ANALYZER_STORE_DATA_STORE_H_
#define COBALT_ANALYZER_STORE_DATA_STORE_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cobalt {
namespace analyzer {
namespace store {

// The status of an operation.
enum Status {
  // The operation succeeded.
  kOK = 0,

  // The operation was not attempted because the arguments are invalid.
  kInvalidArguments,

  // The operation was attempted but failed for an unspecified reason. More
  // information may be found in the log file.
  kOperationFailed
};

// Interface to the Cobalt underlying data store. Instead of working
// directly with this interface, work with ObservationStore and
// ReportStore which are implemented on top of this interface.
//
// The Cobalt data store is a key-multi-value store. There are
// two tables: Observations and Reports. Each table is organized into
// rows identified by a unique string row key. Each row has multiple values
// organized into columns. A single column value has a column_name and a
// column_value, both of which are strings. Different rows may have different
// numbers of columns and different column names.
//
// The rows are ordered lexicographically by row_key.
class DataStore {
 public:
  // The different tables that are controlled by this data store.
  enum Table {
    // The Observations table.
    kObservations,

    // The Reports table.
    kReports
  };

  virtual ~DataStore() = 0;

  // The value of a column contained in a |Row|.
  struct ColumnValue {
    // Constructor
    ColumnValue(std::string name, std::string value)
        : name(std::move(name)), value(std::move(value)) {}

    std::string name;
    std::string value;
  };

  // A row of the data store. A move-only type.
  struct Row {
    // Default constructor
    Row() {}

    // Move constructor
    Row(Row&& other)
        : key(std::move(other.key)),
          column_values(std::move(other.column_values)) {}

    std::string key;
    std::vector<ColumnValue> column_values;
  };

  // Writes a row of |table|. The operation may be an insert of a new row
  // or a replacement of an existing row.
  //
  // Returns kOK on success or an error status on failure.
  virtual Status WriteRow(Table table, Row row) = 0;

  // A ReadResponse is returned from the ReadRows() method.
  struct ReadResponse {
    // status will be kOK on success or an error status on failure.
    // If there was an error then the other fields of ReadResponse
    // should be ignored.
    Status status;

    // If status is kOK then this is the list of returned rows.
    std::vector<Row> rows;

    // If status is kOK, indicates whether or not there were more rows available
    // from the requested range than were returned. If true the caller may
    // invoke ReadRows again, passing as |start_row_key| the key of the last
    // returned row from |rows| and passing |inclusive| = false. Note that it
    // is possible that more_available = true even if rows.size() < max_rows.
    // In other words fewer than max_rows might be returned even if there are
    // more rows available.
    bool more_available;
  };

  // Read a range of rows from the store.
  // table: Which table to read from.
  // start_row_key: The start of the interval to be read.
  // inclusive: Whether or not the interval to be read includes the
  //            |start_row_key|.
  // limit_row_key: The *exclusive* end of the interval to be read. That is,
  //                interval does not include |limit_row_key|.
  //                If |limit_row_key| is empty it is interpreted as the
  //                infinite row key.
  // columns: If non-empty then the read will only return data from the
  //          specified columns. Otherwise there will be no restriction.
  // max_rows: If positive then at most |max_rows| rows will be returned.
  //           Use zero to indicate no desired maximum. The number of
  //           returned rows may be less than max_rows for several reasons
  virtual ReadResponse ReadRows(Table table, std::string start_row_key,
                                bool inclusive, std::string limit_row_key,
                                std::vector<std::string> columns,
                                size_t max_rows) = 0;
};

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_DATA_STORE_H_
