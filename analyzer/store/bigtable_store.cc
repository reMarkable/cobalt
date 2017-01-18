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

#include "analyzer/store/bigtable_store.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <google/bigtable/v2/data.pb.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/store/bigtable_emulator_helper.h"
#include "analyzer/store/bigtable_names.h"
#include "util/crypto_util/base64.h"

using google::bigtable::v2::Bigtable;
using google::bigtable::v2::MutateRowRequest;
using google::bigtable::v2::MutateRowResponse;
using google::bigtable::v2::Mutation_SetCell;
using google::bigtable::v2::ReadRowsRequest;
using google::bigtable::v2::ReadRowsResponse;
using google::bigtable::v2::ReadRowsResponse_CellChunk;
using google::bigtable::v2::RowRange;
using google::bigtable::v2::RowSet;
using grpc::ClientContext;
using grpc::ClientReader;

typedef google::bigtable::admin::v2::Table BtTable;

namespace cobalt {
namespace analyzer {
namespace store {

using crypto::RegexEncode;
using crypto::RegexDecode;

// TODO(rudominer) The default values for these flags should eventually be
// removed. They conveniently point to our Cloud instance for now.
DEFINE_string(project_name, "google.com:shuffler-test",
              "The name of Cobalt's Google Cloud project.");
DEFINE_string(instance_name, "cobalt-analyzer",
              "The name of Cobalt's Google Cloud Bigtable instance.");

DEFINE_bool(for_testing_only_use_bigtable_emulator, false,
            "If --for_cobalt_testing_only_use_memstore=false and this flag "
            "is true then use insecure client credentials to connect to "
            "the Bigtable Emulator running at the default port on localhost.");

std::shared_ptr<BigtableStore> BigtableStore::CreateFromFlagsOrDie() {
  if (FLAGS_for_testing_only_use_bigtable_emulator) {
    LOG(WARNING) << "*** Using an insecure connection to Bigtable Emulator "
                    "instead of using a secure connection to Cloud Bigtable. "
                    "***";
    return std::shared_ptr<BigtableStore>(
        BigtableStoreEmulatorFactory::NewStore());
  }

  // Note that grpc::GoogleDefaultCredentials() uses a Google service account
  // and loads the private keys for this account from the file named in
  // the environment variable GOOGLE_APPLICATION_CREDENTIALS.
  // See http://www.grpc.io/docs/guides/auth.html.
  CHECK_NE("", FLAGS_project_name);
  CHECK_NE("", FLAGS_instance_name);
  auto creds = grpc::GoogleDefaultCredentials();
  CHECK(creds);
  LOG(INFO) << "Connecting to CloudBigtable at " << kCloudBigtableUri;
  return std::shared_ptr<BigtableStore>(new BigtableStore(
      kCloudBigtableUri, creds, FLAGS_project_name, FLAGS_instance_name));
}

BigtableStore::BigtableStore(
    std::string uri, std::shared_ptr<grpc::ChannelCredentials> credentials,
    std::string project_name, std::string instance_name)
    : stub_(
          Bigtable::NewStub(grpc::CreateChannel(std::move(uri), credentials))),
      observations_table_name_(
          BigtableNames::ObservationsTableName(project_name, instance_name)),
      reports_table_name_(BigtableNames::ReportsTableName(
          std::move(project_name), std::move(instance_name))) {}

std::string BigtableStore::TableName(DataStore::Table table) {
  switch (table) {
    case kObservations:
      return observations_table_name_;

    case kReports:
      return reports_table_name_;

    default:
      CHECK(false) << "unexpected table: " << table;
  }
}

Status BigtableStore::WriteRow(DataStore::Table table, DataStore::Row row) {
  MutateRowRequest req;
  req.set_table_name(TableName(table));
  req.mutable_row_key()->swap(row.key);

  for (auto& pair : row.column_values) {
    Mutation_SetCell* cell = req.add_mutations()->mutable_set_cell();
    cell->set_family_name(kDataColumnFamilyName);
    // We Regex encode all values before using them as column names so that
    // we can use a regular expression to search for specific column names
    // later.
    std::string encoded_column_name;
    if (!RegexEncode(pair.first, &encoded_column_name)) {
      LOG(ERROR) << "RegexEncode failed on '" << pair.first << "'";
      return kInvalidArguments;
    }
    cell->mutable_column_qualifier()->swap(encoded_column_name);
    cell->mutable_value()->swap(pair.second);
  }

  ClientContext context;
  MutateRowResponse resp;
  grpc::Status status = stub_->MutateRow(&context, req, &resp);

  if (!status.ok()) {
    // TODO(rudominer) Consider doing a retry here. Consider if this
    // method should be asynchronous.
    LOG(ERROR) << "Error during WriteRow: " << status.error_message();
    return kOperationFailed;
  }

  return kOK;
}

BigtableStore::ReadResponse BigtableStore::ReadRows(
    Table table, std::string start_row_key, bool inclusive,
    std::string limit_row_key, const std::vector<std::string>& column_names,
    size_t max_rows) {
  ReadResponse read_response;
  read_response.status = kOK;
  if (max_rows == 0) {
    LOG(ERROR) << "max_rows=0";
    read_response.status = kInvalidArguments;
    return read_response;
  }

  ReadRowsRequest req;
  req.set_table_name(TableName(table));

  RowSet* rowset = req.mutable_rows();
  RowRange* row_range = rowset->add_row_ranges();

  if (inclusive) {
    row_range->mutable_start_key_closed()->swap(start_row_key);
  } else {
    row_range->mutable_start_key_open()->swap(start_row_key);
  }
  if (!limit_row_key.empty()) {
    row_range->mutable_end_key_open()->swap(limit_row_key);
  }

  if (!column_names.empty()) {
    std::string column_filter;
    bool first = true;
    for (const auto& column_name : column_names) {
      if (!first) {
        column_filter += "|";
      }
      // Our column names are RegexEncoded.
      std::string encoded_column_name;
      if (!RegexEncode(column_name, &encoded_column_name)) {
        LOG(ERROR) << "RegexEncode failed on '" << column_name << "'";
        read_response.status = kOperationFailed;
        return read_response;
      }
      column_filter += encoded_column_name;
      first = false;
    }
    req.mutable_filter()->mutable_column_qualifier_regex_filter()->swap(
        column_filter);
  }

  // We request one more row than we really want in order to be able
  // to set the |more_available| value in the response.
  req.set_rows_limit(max_rows + 1);

  ClientContext context;
  std::unique_ptr<ClientReader<ReadRowsResponse>> reader(
      stub_->ReadRows(&context, req));

  ReadRowsResponse resp;
  size_t num_complete_rows_read = 0;
  // The name of the current column for which we are receiving data. This
  // changes as the server sends us a chunk with a new "qualifier". (In
  // Bigtable lingo the "column qualifier" is what we are calling the column
  // name here.) The column names stored in Bigtable are RegexEncoded, but
  // we want to return the decoded version.
  std::string current_decoded_column_name = "";

  // We are using GRPC's Server Streaming feature to receive the response.
  // reader->Read() returns false to indicate that there will be no more
  // incoming messages, either because all the rows have been transmitted
  // or because the stream has failed or been canceled. (We detect these
  // latter cases by examining the Status returned from reader->Finish().)
  // It appears that it is necessary to keep reading until Read() returns
  // false, even if we have read as many rows as we want, because if we
  // leave the last row unread then the call to reader->Finish() below
  // will hang.
  while (reader->Read(&resp)) {
    for (const auto& chunk : resp.chunks()) {
      if (num_complete_rows_read == max_rows) {
        read_response.more_available = true;
        break;
      }

      // When we get a different row key, start a new row.
      if (read_response.rows.empty() ||
          read_response.rows.back().key != chunk.row_key()) {
        read_response.rows.emplace_back();
        read_response.rows.back().key = chunk.row_key();
        // We are starting a new row so reset the current column.
        current_decoded_column_name = "";
      }
      auto& row = read_response.rows.back();
      if (!chunk.has_qualifier()) {
        // Keep using the same current column.
        CHECK(!current_decoded_column_name.empty());
      } else {
        // Update the current column.
        if (!RegexDecode(chunk.qualifier().value(),
                         &current_decoded_column_name)) {
          read_response.status = kOperationFailed;
          return read_response;
        }
      }
      row.column_values[current_decoded_column_name] += chunk.value();
      if (chunk.commit_row()) {
        num_complete_rows_read++;
      }
      // TODO(rudominer) Handle chunk.reset_row(). For now we fail CHECK if
      // it happens just so we can keep track of if it is happening. But
      // ultimately this CHECK is not correct and we should handle this case
      // because I believe it can actually happen.
      CHECK(!chunk.reset_row());
    }
  }

  grpc::Status status = reader->Finish();

  if (!status.ok()) {
    // TODO(rudominer) Consider doing a retry here. Consider if this
    // method should be asynchronous.
    LOG(ERROR) << "Error during ReadRows: " << status.error_message();
    read_response.status = kOperationFailed;
    return read_response;
  }

  read_response.status = kOK;
  return read_response;
}

Status BigtableStore::DeleteRow(Table table, std::string row_key) {
  MutateRowRequest req;
  req.set_table_name(TableName(table));
  req.mutable_row_key()->swap(row_key);
  req.add_mutations()->mutable_delete_from_row();

  ClientContext context;
  MutateRowResponse resp;
  grpc::Status status = stub_->MutateRow(&context, req, &resp);

  if (!status.ok()) {
    // TODO(rudominer) Consider doing a retry here. Consider if this
    // method should be asynchronous.
    LOG(ERROR) << "Error during DeleteRow: " << status.error_message();
    return kOperationFailed;
  }

  return kOK;
}

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
