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

#include <string>
#include <utility>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {
namespace store {

namespace {
// Generates a row key string based on the given |index|.
std::string RowKeyString(uint32_t index) {
  std::string out(14, 0);
  std::snprintf(&out[0], out.size(), "row%.10u", index);
  return out;
}

// Generates a column name string based on the given |colum_index|.
std::string ColumnNameString(uint32_t column_index) {
  std::string out(17, 0);
  std::snprintf(&out[0], out.size(), "column%.10u", column_index);
  return out;
}

// Generates a value string based on the given |row_index| and |colum_index|.
std::string ValueString(uint32_t row_index, uint32_t column_index) {
  std::string out(27, 0);
  std::snprintf(&out[0], out.size(), "value%.10u:%.10u", row_index,
                column_index);
  return out;
}

std::vector<std::string> MakeColumnNames(int num_columns) {
  std::vector<std::string> column_names;
  for (int column_index = 0; column_index < num_columns; column_index++) {
    column_names.push_back(ColumnNameString(column_index));
  }
  return column_names;
}

void AddRows(int num_columns, int num_rows) {
  MemoryStore mem_store;
  mem_store.DeleteAllRows(DataStore::kObservations);
  std::vector<std::string> column_names = MakeColumnNames(num_columns);
  for (int row_index = 0; row_index < num_rows; row_index++) {
    DataStore::Row row;
    row.key = RowKeyString(row_index);
    for (int column_index = 0; column_index < num_columns; column_index++) {
      row.column_values[column_names[column_index]] =
          ValueString(row_index, column_index);
    }
    mem_store.WriteRow(DataStore::kObservations, std::move(row));
  }
}

// Counts the total number of rows in the store.
void CheckNumRows(int expected_num_rows) {
  MemoryStore mem_store;
  std::vector<std::string> column_names;

  size_t num_rows_counted = 0;
  std::string first_row = "";
  while (true) {
    DataStore::ReadResponse read_response =
        mem_store.ReadRows(DataStore::kObservations, first_row, false, "",
                           column_names, UINT32_MAX);
    EXPECT_EQ(kOK, read_response.status);
    size_t num_rows_read = read_response.rows.size();
    num_rows_counted += num_rows_read;
    if (!read_response.more_available) {
      break;
    }
    first_row = read_response.rows[num_rows_read - 1].key;
  }

  EXPECT_EQ(expected_num_rows, num_rows_counted);
}

// set limit_row = -1 to indicate an unbounded range.
void ReadRowsAndCheck(int num_columns, int start_row, bool inclusive,
                      int limit_row, int max_rows, int expected_num_rows,
                      bool expect_more_available) {
  MemoryStore mem_store;
  std::vector<std::string> column_names = MakeColumnNames(num_columns);

  std::string limit_row_key;
  if (limit_row < 0) {
    limit_row_key = "";
  } else {
    limit_row_key = RowKeyString(limit_row);
  }
  DataStore::ReadResponse read_response =
      mem_store.ReadRows(DataStore::kObservations, RowKeyString(start_row),
                         inclusive, limit_row_key, column_names, max_rows);

  EXPECT_EQ(kOK, read_response.status);
  EXPECT_EQ(expected_num_rows, read_response.rows.size());
  int row_index = (inclusive ? start_row : start_row + 1);
  for (const DataStore::Row& row : read_response.rows) {
    EXPECT_EQ(RowKeyString(row_index), row.key);
    EXPECT_EQ(num_columns, row.column_values.size());
    for (int column_index = 0; column_index < num_columns; column_index++) {
      EXPECT_EQ(ValueString(row_index, column_index),
                row.column_values.at(ColumnNameString(column_index)));
    }
    row_index++;
  }
  EXPECT_EQ(expect_more_available, read_response.more_available);
}

void DeleteRows(int start_row, bool inclusive, int limit_row) {
  MemoryStore mem_store;
  std::string limit_row_key;
  if (limit_row < 0) {
    limit_row_key = "";
  } else {
    limit_row_key = RowKeyString(limit_row);
  }
  Status status =
      mem_store.DeleteRows(DataStore::kObservations, RowKeyString(start_row),
                           inclusive, limit_row_key);

  EXPECT_EQ(kOK, status);
}

}  // namespace

TEST(MemoryStoreTest, WriteAndReadRows) {
  // Add 1000 rows of 3 columns each.
  AddRows(3, 1000);

  // Read rows [100, 175) with max_rows = 50. Expect 50 rows with more
  // available.
  ReadRowsAndCheck(3, 100, true, 175, 50, 50, true);

  // Read rows (100, 175) with max_rows = 50. Expect 50 rows with more
  // available.
  ReadRowsAndCheck(3, 100, false, 175, 50, 50, true);

  // Read rows [100, 175) with max_rows = 80. Expect 75 rows with no more
  // available.
  ReadRowsAndCheck(3, 100, true, 175, 80, 75, false);

  // Read rows (100, 175) with max_rows = 80. Expect 74 rows with no more
  // available.
  ReadRowsAndCheck(3, 100, false, 175, 80, 74, false);

  // Read rows [100, 300) with max_rows = 100. Expect 100 rows with
  // more  available.
  ReadRowsAndCheck(3, 100, true, 300, 100, 100, true);

  // Read rows (100, 300) with max_rows=100. Expect 100 rows with
  // more  available.
  ReadRowsAndCheck(3, 100, false, 300, 100, 100, true);
}

// Tests reading an unbounded range.
TEST(MemoryStoreTest, UnboundedRange) {
  // Add 1000 rows of 3 columns each.
  AddRows(3, 1000);

  // Read rows [100, infinity) with max_rows = 50. Expect 50 rows with more
  // available.
  ReadRowsAndCheck(3, 100, true, -1, 50, 50, true);

  // Read rows (100, infinity) with max_rows = 50. Expect 50 rows with more
  // available.
  ReadRowsAndCheck(3, 100, false, -1, 50, 50, true);

  // Read rows [950, infinity) with max_rows=100. Expect 50 rows with
  // no more  available.
  ReadRowsAndCheck(3, 950, true, -1, 100, 50, false);

  // Read rows (950, infinity) with max_rows=100. Expect 49 rows with
  // no more  available.
  ReadRowsAndCheck(3, 950, false, -1, 100, 49, false);
}

// Tests deleting ranges of rows.
TEST(MemoryStoreTest, DeleteRanges) {
  MemoryStore mem_store;
  mem_store.DeleteAllRows(DataStore::kObservations);

  // Initially there should be no rows.
  CheckNumRows(0);

  // Add 1000 rows of 1 column each.
  AddRows(1, 1000);
  // Now there should be 1000 rows.
  CheckNumRows(1000);

  // Delete rows [100, 200)
  DeleteRows(100, true, 200);
  CheckNumRows(900);

  // Delete rows (500, 700)
  DeleteRows(500, false, 700);
  CheckNumRows(701);

  // Delete rows [0, 1)
  DeleteRows(0, true, 1);
  CheckNumRows(700);

  // Delete rows [0, 10000)
  DeleteRows(0, true, 10000);
  CheckNumRows(0);
}

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
