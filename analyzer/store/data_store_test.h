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

#ifndef COBALT_ANALYZER_STORE_DATA_STORE_TEST_H_
#define COBALT_ANALYZER_STORE_DATA_STORE_TEST_H_

#include "analyzer/store/data_store.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

// This file contains type-parameterized tests of the DataStore interface.
//
// We use C++ templates along with the macros TYPED_TEST_CASE_P and
// TYPED_TEST_P in order to define test templates that may be instantiated to
// to produce concrete tests of various implementations of the DataStore
// interface.
//
// See memory_store_test.cc and bigtable_store_emulator_test.cc for the
// concrete instantiations.
//
// NOTE: If you add a new test to this file you must add its name to the
// invocation REGISTER_TYPED_TEST_CASE_P macro at the bottom of this file.

namespace cobalt {
namespace analyzer {
namespace store {

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

// Makes a vector of column name strings for |num_columns| columns.
std::vector<std::string> MakeColumnNames(int num_columns) {
  std::vector<std::string> column_names;
  for (int column_index = 0; column_index < num_columns; column_index++) {
    column_names.push_back(ColumnNameString(column_index));
  }
  return column_names;
}

// DataStoreTest is templatized on the parameter |StoreFactoryClass| which
// must be the name of a class that contains the following method:
//   static DataStore* NewStore()
// See MemoryStoreFactory in memory_store_test_helper.h for example.
//
// Note: For simplicity we perform all tests using the Observations table
// only.
template <class StoreFactoryClass>
class DataStoreTest : public ::testing::Test {
 protected:
  DataStoreTest() : data_store_(StoreFactoryClass::NewStore()) {
    data_store_->DeleteAllRows(DataStore::kObservations);
    EXPECT_EQ(kOK, data_store_->DeleteAllRows(DataStore::kObservations));
  }

  // Adds |num_rows| rows with |num_columns| columns each.
  void AddRows(int num_columns, int num_rows);

  // Counts the total number of rows in the store.
  void CheckNumRows(int expected_num_rows);

  // Reads the specified number of columns from the specified row range
  // and checks that the results are as expected.
  //
  // Set limit_row = -1 to indicate an unbounded range.
  void ReadRowsAndCheck(int num_columns, int start_row, bool inclusive,
                        int limit_row, int max_rows, int expected_num_rows,
                        bool expect_more_available);

  void DeleteRows(int start_row, bool inclusive, int limit_row);

  std::unique_ptr<DataStore> data_store_;
};

template <class StoreFactoryClass>
void DataStoreTest<StoreFactoryClass>::AddRows(int num_columns, int num_rows) {
  std::vector<std::string> column_names = MakeColumnNames(num_columns);
  for (int row_index = 0; row_index < num_rows; row_index++) {
    DataStore::Row row;
    row.key = RowKeyString(row_index);
    for (int column_index = 0; column_index < num_columns; column_index++) {
      row.column_values[column_names[column_index]] =
          ValueString(row_index, column_index);
    }
    data_store_->WriteRow(DataStore::kObservations, std::move(row));
  }
}

template <class StoreFactoryClass>
void DataStoreTest<StoreFactoryClass>::CheckNumRows(int expected_num_rows) {
  std::vector<std::string> column_names;

  DataStore::ReadResponse read_response =
      data_store_->ReadRows(DataStore::kObservations, RowKeyString(0), true, "",
                            column_names, UINT32_MAX);

  EXPECT_EQ(kOK, read_response.status);
  EXPECT_EQ(expected_num_rows, read_response.rows.size());
}

template <class StoreFactoryClass>
void DataStoreTest<StoreFactoryClass>::ReadRowsAndCheck(
    int num_columns, int start_row, bool inclusive, int limit_row, int max_rows,
    int expected_num_rows, bool expect_more_available) {
  std::vector<std::string> column_names = MakeColumnNames(num_columns);

  std::string limit_row_key;
  if (limit_row < 0) {
    limit_row_key = "";
  } else {
    limit_row_key = RowKeyString(limit_row);
  }
  DataStore::ReadResponse read_response =
      data_store_->ReadRows(DataStore::kObservations, RowKeyString(start_row),
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

template <class StoreFactoryClass>
void DataStoreTest<StoreFactoryClass>::DeleteRows(int start_row, bool inclusive,
                                                  int limit_row) {
  std::string limit_row_key;
  if (limit_row < 0) {
    limit_row_key = "";
  } else {
    limit_row_key = RowKeyString(limit_row);
  }
  Status status =
      data_store_->DeleteRows(DataStore::kObservations, RowKeyString(start_row),
                              inclusive, limit_row_key);

  EXPECT_EQ(kOK, status);
}

TYPED_TEST_CASE_P(DataStoreTest);

TYPED_TEST_P(DataStoreTest, WriteAndReadRows) {
  // Add 1000 rows of 3 columns each.
  this->AddRows(3, 1000);

  // Read rows [100, 175) with max_rows = 50. Expect 50 rows with more
  // available.
  size_t max_rows = 50;
  size_t expected_rows = 50;
  this->ReadRowsAndCheck(3, 100, true, 175, max_rows, expected_rows, true);

  // Read rows (100, 175) with max_rows = 50. Expect 50 rows with more
  // available.
  this->ReadRowsAndCheck(3, 100, false, 175, max_rows, expected_rows, true);

  // Read rows [100, 175) with max_rows = 80. Expect 75 rows with no more
  // available.
  max_rows = 80;
  expected_rows = 75;
  this->ReadRowsAndCheck(3, 100, true, 175, max_rows, expected_rows, false);

  // Read rows (100, 175) with max_rows = 80. Expect 74 rows with no more
  // available.
  max_rows = 80;
  expected_rows = 74;
  this->ReadRowsAndCheck(3, 100, false, 175, max_rows, expected_rows, false);

  // Read rows [100, 300) with max_rows = 100. Expect 100 rows with
  // more  available.
  max_rows = 100;
  expected_rows = 100;
  this->ReadRowsAndCheck(3, 100, true, 300, max_rows, expected_rows, true);

  // Read rows (100, 300) with max_rows = 100. Expect 100 rows with
  // more  available.
  this->ReadRowsAndCheck(3, 100, false, 300, max_rows, expected_rows, true);

  // Read rows [0, 0) with max_rows = 100. Expect 0 rows with
  // more no available.
  max_rows = 100;
  expected_rows = 0;
  this->ReadRowsAndCheck(3, 0, true, 0, max_rows, expected_rows, false);

  // Read rows [0, 1) with max_rows = 100. Expect 1 row with
  // more no available.
  max_rows = 100;
  expected_rows = 1;
  this->ReadRowsAndCheck(3, 0, true, 1, max_rows, expected_rows, false);
}

// Tests reading an unbounded range.
TYPED_TEST_P(DataStoreTest, UnboundedRange) {
  // Add 1000 rows of 3 columns each.
  this->AddRows(3, 1000);

  // Read rows [100, infinity) with max_rows = 50. Expect 50 rows with more
  // available.
  size_t max_rows = 50;
  size_t expected_rows = 50;
  this->ReadRowsAndCheck(3, 100, true, -1, max_rows, expected_rows, true);

  // Read rows (100, infinity) with max_rows = 50. Expect 50 rows with more
  // available.
  this->ReadRowsAndCheck(3, 100, false, -1, max_rows, expected_rows, true);

  // Read rows [100, infinity) with max_rows = 100. Expect 100 rows with
  // more  available.
  max_rows = 100;
  expected_rows = 100;
  this->ReadRowsAndCheck(3, 100, true, -1, max_rows, expected_rows, true);

  // Read rows (100, infinity) with max_rows = 100. Expect 100 rows with
  // more  available.
  this->ReadRowsAndCheck(3, 100, false, -1, max_rows, expected_rows, true);

  // Read rows [950, infinity) with max_rows = 100. Expect 50 rows with
  // no more  available.
  expected_rows = 50;
  this->ReadRowsAndCheck(3, 950, true, -1, max_rows, expected_rows, false);

  // Read rows (950, infinity) with max_rows = 100 Expect 49 rows with
  // no more  available.
  expected_rows = 49;
  this->ReadRowsAndCheck(3, 950, false, -1, max_rows, expected_rows, false);

  // Read rows [0, infinity) with max_rows = 10,000. Expect 1,000 rows with
  // no more  available.
  max_rows = 10000;
  expected_rows = 1000;
  this->ReadRowsAndCheck(3, 0, true, -1, max_rows, expected_rows, false);

  // Read rows [0, infinity) with max_rows = 1,000, Expect 1,000 rows with
  // no more  available.
  max_rows = 1000;
  expected_rows = 1000;
  this->ReadRowsAndCheck(3, 0, true, -1, max_rows, expected_rows, false);

  // Read rows [0, infinity) with max_rows = 999, Expect 999 rows with
  // more  available.
  max_rows = 999;
  expected_rows = 999;
  this->ReadRowsAndCheck(3, 0, true, -1, max_rows, expected_rows, true);
}

// Tests deleting ranges of rows.
TYPED_TEST_P(DataStoreTest, DeleteRanges) {
  // Initially there should be no rows.
  this->CheckNumRows(0);

  // Add 1000 rows of 1 column each.
  this->AddRows(1, 1000);
  // Now there should be 1000 rows.
  this->CheckNumRows(1000);

  // Delete rows [100, 200)
  this->DeleteRows(100, true, 200);
  this->CheckNumRows(900);

  // Delete rows (500, 700)
  this->DeleteRows(500, false, 700);
  this->CheckNumRows(701);

  // Delete rows [0, 1)
  this->DeleteRows(0, true, 1);
  this->CheckNumRows(700);

  // Delete rows [0, 10000)
  this->DeleteRows(0, true, 10000);
  this->CheckNumRows(0);
}

REGISTER_TYPED_TEST_CASE_P(DataStoreTest, WriteAndReadRows, UnboundedRange,
                           DeleteRanges);

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_DATA_STORE_TEST_H_
