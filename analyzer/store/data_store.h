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

  // The requested item was not found.
  kNotFound,

  // The item being created already exists.
  kAlreadyExists,

  // The operation requires a pre-condition which is not true.
  kPreconditionFailed,

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
// organized into columns. Each column has a string name and a string value.
// Different rows may have different numbers of columns and different column
// names.
//
// The rows are ordered lexicographically by row_key.
class DataStore {
 public:
  static std::unique_ptr<DataStore> CreateFromFlagsOrDie();

  // The different tables that are controlled by this data store.
  enum Table {
    // The Observations table holds the Observations received from the Shuffler.
    kObservations,

    // The ReportMetadata table holds metadata about reports.
    kReportMetadata,

    // The ReportRows table holds the actual rows of reports.
    kReportRows,
  };

  virtual ~DataStore() = 0;

  // A row of the data store. A move-only type.
  struct Row {
    // Default constructor
    Row() {}

    // Move constructor
    Row(Row&& other)
        : key(std::move(other.key)),
          column_values(std::move(other.column_values)) {}

    // The row key
    std::string key;

    // The column values. The keys of the map are the column names and the
    // values of the map are the column values.
    std::map<std::string, std::string> column_values;
  };

  // Writes a row of |table|. The operation may be an insert of a new row
  // or a replacement of an existing row.
  //
  // Returns kOK on success or an error status on failure.
  virtual Status WriteRow(Table table, Row row) = 0;

  // Writes many rows of |table|. The operation may consists of inserts of
  // new rows and replacements of existing rows.
  //
  // The sum over all of the rows of the number of columns being written
  // must be less than 100,000.
  //
  // Returns kOK on success or an error status on failure.
  virtual Status WriteRows(Table table, std::vector<Row> rows) = 0;

  // Reads the row with the given key from the store, if there is one.
  //
  // table: Which table to read from.
  //
  // column_names: If non-empty then the read will only return data from the
  //     columns with the specified names. Otherwise there will be no
  //     restriction.
  //
  // row: is both input and output. On input only the |key| field will be
  //     inspected and the |column_values| will be cleared. The row with the
  //     given |key| will be fetched from the datastore.
  //
  // Returns kOK on success, kNotFound if there is no such row,
  // kInvalidArgument if |row| is nullptr, and kOperationFailed if there was
  // any other unexpected problem.
  virtual Status ReadRow(Table table,
                         const std::vector<std::string>& column_names,
                         Row* row) = 0;

  // A ReadResponse is returned from the ReadRows() method.
  struct ReadResponse {
    // status will be kOK on success or an error status on failure.
    // If there was an error then the other fields of ReadResponse
    // should be ignored.
    Status status;

    // If status is kOK then this is the list of returned rows. If
    // |more_available| is true then there will be at least one row.
    std::vector<Row> rows;

    // If status is kOK, indicates whether or not there were more rows available
    // from the requested range than were returned. If true the caller may
    // invoke ReadRows again, passing as |start_row_key| the key of the last
    // returned row from |rows| and passing |inclusive| = false. Note that it
    // is possible that more_available = true even if rows.size() < max_rows.
    // In other words fewer than max_rows might be returned even if there are
    // more rows available. However if |more_availabe| is true then it
    // is guaranteed that |rows| will not be empty.
    bool more_available = false;
  };

  // Reads a lexicographic range of rows from the store.
  //
  // table: Which table to read from.
  //
  // start_row_key: The start of the lexicographic interval to be read.
  //
  // inclusive: Whether or not the interval to be read includes the
  //    |start_row_key|.
  //
  // limit_row_key: The *exclusive* end of the interval to be read. That is,
  //     the interval does not include |limit_row_key|. If |limit_row_key| is
  //     empty it is interpreted as the infinite row key. |start_row_key| must
  //     be less than |limit_row_key| lexicographically.
  //
  // column_names: If non-empty then the read will only return data from the
  //     columns with the specified names. Otherwise there will be no
  //     restriction.
  //
  // max_rows: At most |max_rows| rows will be returned. The number of
  //     returned rows may be less than max_rows for several reasons.
  //     Must be positive or kInvalidArguments will be returned.
  virtual ReadResponse ReadRows(Table table, std::string start_row_key,
                                bool inclusive, std::string limit_row_key,
                                const std::vector<std::string>& column_names,
                                size_t max_rows) = 0;

  // Deletes the given row from the given table, if it exists.
  virtual Status DeleteRow(Table table, std::string row_key) = 0;

  // Deletes the rows from the store whose row keys contain the given
  // |row_key_prefix| as a prefix.
  //
  // table: Which table to delete from.
  //
  // row_key_prefix: All rows with row keys that extend this prefix will be
  //     deleted. |row_key_prefix| cannot be empty. To delete all rows use
  //     DeleteAllRows().
  virtual Status DeleteRowsWithPrefix(Table table,
                                      std::string row_key_prefix) = 0;

  // Deletes all of the rows of the given table.
  //
  // WARNING: This permanently deletes all data from the table.
  virtual Status DeleteAllRows(Table table) = 0;
};

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_DATA_STORE_H_
