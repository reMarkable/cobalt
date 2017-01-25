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

#include "gflags/gflags.h"
#include "glog/logging.h"
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

static const int kNumColumns = 3;

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
  DataStoreTest() : data_store_(StoreFactoryClass::NewStore()) {}

  void SetUp() {
    EXPECT_EQ(kOK, data_store_->DeleteAllRows(DataStore::kObservations));
    EXPECT_EQ(0, GetNumRows());
  }

  // Adds |num_rows| rows with kNumColumns columns each.
  void AddRows(int num_rows);

  // Returns the total number of rows in the store.
  size_t GetNumRows();

  // Reads the specified number of columns from the specified row and checks
  // that the result is as expected.
  //
  // If num_columns = 0 then no columns are specified in the read and therefore
  // all columns should be returned and so the expected num_columns is
  // kNumColumns.
  void ReadSingleRowAndCheck(int num_columns, int row_idnex,
                             bool expect_row_found);

  // Reads the specified number of columns from the specified row range
  // and checks that the results are as expected.
  //
  // If num_columns = 0 then no columns are specified in the read and therefore
  // all columns should be returned and so the expected num_columns is
  // kNumColumns.
  //
  // Set limit_row = -1 to indicate an unbounded range.
  void ReadRowsAndCheck(int num_columns, int start_row, bool inclusive,
                        int limit_row, int max_rows, int expected_num_rows,
                        bool expect_more_available);

  void DeleteRowsWithPrefix(int basis, int suffix_length);

  // In order to work around the following bug in the Bigtable Emulator
  // https://github.com/GoogleCloudPlatform/google-cloud-go/issues/489
  // we use a different set of row keys for each test. Each row created during
  // a test will be prefixed with |test_prefix|.
  void set_test_prefix(std::string test_prefix) {
    test_prefix_ = std::move(test_prefix);
  }

  // Generates a row key string based on the given |index| and prefix.
  static std::string RowKeyString(const std::string& prefix, uint32_t index) {
    // We allocate a buffer of size 14 to leave room for the trailing null.
    std::string out(14, 0);
    std::snprintf(&out[0], out.size(), "row%.10u", index);
    // Discard the trailing null.
    out.resize(13);
    return prefix + out;
  }

  // Generates a column name string based on the given |colum_index|.
  static std::string ColumnNameString(uint32_t column_index) {
    std::string out(17, 0);
    std::snprintf(&out[0], out.size(), "column%.10u", column_index);
    return out;
  }

  // Generates a value string based on the given |row_index| and |colum_index|.
  static std::string ValueString(uint32_t row_index, uint32_t column_index) {
    std::string out(27, 0);
    std::snprintf(&out[0], out.size(), "value%.10u:%.10u", row_index,
                  column_index);
    return out;
  }

  // Makes a vector of column name strings for |num_columns| columns.
  static std::vector<std::string> MakeColumnNames(size_t num_columns) {
    std::vector<std::string> column_names;
    for (int column_index = 0; column_index < num_columns; column_index++) {
      column_names.push_back(ColumnNameString(column_index));
    }
    return column_names;
  }

  std::unique_ptr<DataStore> data_store_;
  std::string test_prefix_;
};

template <class StoreFactoryClass>
void DataStoreTest<StoreFactoryClass>::AddRows(int num_rows) {
  std::vector<std::string> column_names = MakeColumnNames(kNumColumns);
  std::vector<DataStore::Row> rows;
  for (int row_index = 0; row_index < num_rows; row_index++) {
    DataStore::Row row;
    row.key = RowKeyString(test_prefix_, row_index);
    for (int column_index = 0; column_index < kNumColumns; column_index++) {
      row.column_values[column_names[column_index]] =
          ValueString(row_index, column_index);
    }
    rows.emplace_back(std::move(row));
  }
  EXPECT_EQ(kOK,
            data_store_->WriteRows(DataStore::kObservations, std::move(rows)));
}

template <class StoreFactoryClass>
size_t DataStoreTest<StoreFactoryClass>::GetNumRows() {
  std::vector<std::string> column_names;

  DataStore::ReadResponse read_response = data_store_->ReadRows(
      DataStore::kObservations, RowKeyString(test_prefix_, 0), true, "",
      column_names, UINT32_MAX);

  EXPECT_EQ(kOK, read_response.status);
  return read_response.rows.size();
}

template <class StoreFactoryClass>
void DataStoreTest<StoreFactoryClass>::ReadSingleRowAndCheck(
    int num_columns, int row_index, bool expect_row_found) {
  std::vector<std::string> column_names = MakeColumnNames(num_columns);
  DataStore::Row row;
  row.key = RowKeyString(test_prefix_, row_index);
  auto status =
      data_store_->ReadRow(DataStore::kObservations, column_names, &row);
  if (expect_row_found) {
    ASSERT_EQ(kOK, status);
  } else {
    EXPECT_EQ(kNotFound, status);
    return;
  }
  int expected_num_columns = (num_columns == 0 ? kNumColumns : num_columns);
  for (int column_index = 0; column_index < expected_num_columns;
       column_index++) {
    EXPECT_EQ(ValueString(row_index, column_index),
              row.column_values.at(ColumnNameString(column_index)));
  }
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
    limit_row_key = RowKeyString(test_prefix_, limit_row);
  }
  DataStore::ReadResponse read_response = data_store_->ReadRows(
      DataStore::kObservations, RowKeyString(test_prefix_, start_row),
      inclusive, limit_row_key, column_names, max_rows);

  EXPECT_EQ(kOK, read_response.status);
  EXPECT_EQ(expected_num_rows, read_response.rows.size());
  int expected_num_columns = (num_columns == 0 ? kNumColumns : num_columns);
  int row_index = (inclusive ? start_row : start_row + 1);
  for (const DataStore::Row& row : read_response.rows) {
    EXPECT_EQ(RowKeyString(test_prefix_, row_index), row.key);
    EXPECT_EQ(expected_num_columns, row.column_values.size());
    for (int column_index = 0; column_index < expected_num_columns;
         column_index++) {
      EXPECT_EQ(ValueString(row_index, column_index),
                row.column_values.at(ColumnNameString(column_index)));
    }
    row_index++;
  }
  EXPECT_EQ(expect_more_available, read_response.more_available);
}

template <class StoreFactoryClass>
void DataStoreTest<StoreFactoryClass>::DeleteRowsWithPrefix(int basis,
                                                            int suffix_length) {
  std::string prefix = RowKeyString(test_prefix_, basis);
  prefix.resize(prefix.size() - suffix_length);
  Status status =
      data_store_->DeleteRowsWithPrefix(DataStore::kObservations, prefix);

  EXPECT_EQ(kOK, status);
}

TYPED_TEST_CASE_P(DataStoreTest);

TYPED_TEST_P(DataStoreTest, WriteAndReadRows) {
  // Add 3000 rows of 3 columns each.
  this->AddRows(3000);

  // Read row number 0, expect it to exist.
  this->ReadSingleRowAndCheck(kNumColumns, 0, true);

  // Read row number 1234, expect it to exist.
  this->ReadSingleRowAndCheck(kNumColumns, 1234, true);

  // Read row number 2999, expect it to exist.
  this->ReadSingleRowAndCheck(kNumColumns, 2999, true);

  // Read row number 3000, expect it to not exist.
  this->ReadSingleRowAndCheck(kNumColumns, 3000, false);

  // Read rows [100, 175) with max_rows = 50. Expect 50 rows with more
  // available.
  size_t max_rows = 50;
  size_t expected_rows = 50;
  this->ReadRowsAndCheck(kNumColumns, 100, true, 175, max_rows, expected_rows,
                         true);

  // Read rows (100, 175) with max_rows = 50. Expect 50 rows with more
  // available.
  this->ReadRowsAndCheck(kNumColumns, 100, false, 175, max_rows, expected_rows,
                         true);

  // Read rows [100, 175) with max_rows = 80. Expect 75 rows with no more
  // available.
  max_rows = 80;
  expected_rows = 75;
  this->ReadRowsAndCheck(kNumColumns, 100, true, 175, max_rows, expected_rows,
                         false);

  // Read rows (100, 175) with max_rows = 80. Expect 74 rows with no more
  // available.
  max_rows = 80;
  expected_rows = 74;
  this->ReadRowsAndCheck(kNumColumns, 100, false, 175, max_rows, expected_rows,
                         false);

  // Read rows [100, 2100) with max_rows = 100. Expect 100 rows with
  // more  available.
  max_rows = 100;
  expected_rows = 100;
  this->ReadRowsAndCheck(kNumColumns, 100, true, 2100, max_rows, expected_rows,
                         true);

  // Read rows (100, 2100) with max_rows = 100. Expect 100 rows with
  // more  available.
  this->ReadRowsAndCheck(kNumColumns, 100, false, 2100, max_rows, expected_rows,
                         true);

  // Read rows (100, 2100) with max_rows = UINT32_MAX. Expect 1999 rows with
  // no more  available.
  max_rows = UINT32_MAX;
  expected_rows = 1999;
  this->ReadRowsAndCheck(kNumColumns, 100, false, 2100, max_rows, expected_rows,
                         false);

  // Read rows [0, 1) with max_rows = 100. Expect 1 row with
  // more no available.
  max_rows = 100;
  expected_rows = 1;
  this->ReadRowsAndCheck(kNumColumns, 0, true, 1, max_rows, expected_rows,
                         false);
}

// Tests reading an unbounded range.
TYPED_TEST_P(DataStoreTest, UnboundedRange) {
  this->set_test_prefix("UnboundedRange");
  // Add 1000 rows of 3 columns each.
  this->AddRows(1000);
  ASSERT_EQ(1000, this->GetNumRows());

  // Read rows [100, infinity) with max_rows = 50. Expect 50 rows with more
  // available.
  size_t max_rows = 50;
  size_t expected_rows = 50;
  this->ReadRowsAndCheck(kNumColumns, 100, true, -1, max_rows, expected_rows,
                         true);

  // Read rows (100, infinity) with max_rows = 50. Expect 50 rows with more
  // available.
  this->ReadRowsAndCheck(kNumColumns, 100, false, -1, max_rows, expected_rows,
                         true);

  // Read rows [100, infinity) with max_rows = 100. Expect 100 rows with
  // more  available.
  max_rows = 100;
  expected_rows = 100;
  this->ReadRowsAndCheck(kNumColumns, 100, true, -1, max_rows, expected_rows,
                         true);

  // Read rows (100, infinity) with max_rows = 100. Expect 100 rows with
  // more  available.
  this->ReadRowsAndCheck(kNumColumns, 100, false, -1, max_rows, expected_rows,
                         true);

  // Read rows [950, infinity) with max_rows = 100. Expect 50 rows with
  // no more  available.
  expected_rows = 50;
  this->ReadRowsAndCheck(kNumColumns, 950, true, -1, max_rows, expected_rows,
                         false);

  // Read rows (950, infinity) with max_rows = 100 Expect 49 rows with
  // no more  available.
  expected_rows = 49;
  this->ReadRowsAndCheck(kNumColumns, 950, false, -1, max_rows, expected_rows,
                         false);

  // Read rows [0, infinity) with max_rows = 10,000. Expect 1,000 rows with
  // no more  available.
  max_rows = 10000;
  expected_rows = 1000;
  this->ReadRowsAndCheck(kNumColumns, 0, true, -1, max_rows, expected_rows,
                         false);

  // Read rows [0, infinity) with max_rows = 1,000, Expect 1,000 rows with
  // no more  available.
  max_rows = 1000;
  expected_rows = 1000;
  this->ReadRowsAndCheck(kNumColumns, 0, true, -1, max_rows, expected_rows,
                         false);

  // Read rows [0, infinity) with max_rows = 999, Expect 999 rows with
  // more  available.
  max_rows = 999;
  expected_rows = 999;
  this->ReadRowsAndCheck(kNumColumns, 0, true, -1, max_rows, expected_rows,
                         true);
}

TYPED_TEST_P(DataStoreTest, ReadDifferentNumColumns) {
  this->set_test_prefix("ReadDifferentNumColumns");
  // Add 10 rows of 3 columns each.
  this->AddRows(10);
  ASSERT_EQ(10, this->GetNumRows());

  // Read rows [3, 6). Expect 3 rows with no more available.
  size_t max_rows = UINT32_MAX;
  size_t expected_rows = 3;

  // Try the read with different numbers of columns specified to read.
  for (size_t num_columns = 0; num_columns <= kNumColumns; num_columns++) {
    this->ReadRowsAndCheck(num_columns, 3, true, 6, max_rows, expected_rows,
                           false);
  }

  // Read row 8 alone.
  // Try the read with different numbers of columns specified to read.
  for (size_t num_columns = 0; num_columns <= kNumColumns; num_columns++) {
    this->ReadSingleRowAndCheck(num_columns, 8, true);
  }
}

// Tests deleting ranges of rows.
TYPED_TEST_P(DataStoreTest, DeleteRanges) {
  this->set_test_prefix("DeleteRanges");
  // Initially there should be no rows.
  ASSERT_EQ(0, this->GetNumRows());

  // Add 3000 rows.
  this->AddRows(3000);
  // Now there should be 3000 rows.
  ASSERT_EQ(3000, this->GetNumRows());

  // Delete 10^0 rows starting with row 100.
  // i.e. delete row 100
  this->DeleteRowsWithPrefix(100, 0);
  ASSERT_EQ(2999, this->GetNumRows());

  // Delete 10^1 rows starting with row 200.
  // i.e. delete rows [200, 209]
  this->DeleteRowsWithPrefix(200, 1);
  ASSERT_EQ(2989, this->GetNumRows());

  // Delete 10^2 rows starting with row 300.
  // i.e. delete rows [300, 399]
  this->DeleteRowsWithPrefix(300, 2);
  ASSERT_EQ(2889, this->GetNumRows());

  // Delete 10^3 rows starting with row 0.
  // i.e. delete rows [0, 999]
  this->DeleteRowsWithPrefix(0, 3);
  ASSERT_EQ(2000, this->GetNumRows());

  // Delete 10^3 rows starting with row 1000.
  // i.e. delete rows [1000, 1999]
  this->DeleteRowsWithPrefix(1000, 3);
  ASSERT_EQ(1000, this->GetNumRows());

  // Delete 10^4 rows starting with row 0.
  // i.e. delete rows [0, 9999]
  this->DeleteRowsWithPrefix(0, 4);
  ASSERT_EQ(0, this->GetNumRows());
}

REGISTER_TYPED_TEST_CASE_P(DataStoreTest, WriteAndReadRows, UnboundedRange,
                           ReadDifferentNumColumns, DeleteRanges);

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_DATA_STORE_TEST_H_
