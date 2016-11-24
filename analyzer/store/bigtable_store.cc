// Copyright 2016 The Fuchsia Authors
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

#include <glog/logging.h>
#include <google/bigtable/v2/data.pb.h>

#include <map>
#include <string>

using google::bigtable::v2::Bigtable;
using google::bigtable::v2::MutateRowRequest;
using google::bigtable::v2::MutateRowResponse;
using google::bigtable::v2::Mutation_SetCell;
using google::bigtable::v2::ReadRowsRequest;
using google::bigtable::v2::ReadRowsResponse;
using google::bigtable::v2::ReadRowsResponse_CellChunk;
using google::bigtable::v2::RowSet;
using google::bigtable::v2::RowRange;
using google::bigtable::admin::v2::BigtableTableAdmin;
using google::bigtable::admin::v2::ColumnFamily;
using google::bigtable::admin::v2::CreateTableRequest;
using google::bigtable::admin::v2::GetTableRequest;
using google::bigtable::admin::v2::Table;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::Status;

namespace cobalt {
namespace analyzer {

BigtableStore::BigtableStore(const std::string& table_name)
    : table_name_(table_name) {}

int BigtableStore::initialize(bool should_init_schema) {
  int rc;

  if ((rc = setup_connection()))
    return rc;

  if (should_init_schema && (rc = init_schema()))
    return rc;

  return 0;
}

// Connects to bigtable or the emulator.
int BigtableStore::setup_connection() {
  std::shared_ptr<grpc::ChannelCredentials> creds;
  const char* host_data = "bigtable.googleapis.com";
  const char* host_admin = "bigtableadmin.googleapis.com";

  // Check to see if we're running on the emulator.
  const char* emulator = getenv("BIGTABLE_EMULATOR_HOST");

  if (emulator) {
    LOG(INFO) << "Using the Bigtable emulator";
    host_data = host_admin = emulator;
    creds = grpc::InsecureChannelCredentials();
  } else {
    // GOOGLE_APPLICATION_CREDENTIALS must be set or you'll hit asserts when
    // running locally and target Bigtable on Google Cloud.
    creds = grpc::GoogleDefaultCredentials();
  }

  data_ = Bigtable::NewStub(grpc::CreateChannel(host_data, creds));
  admin_ = BigtableTableAdmin::NewStub(grpc::CreateChannel(host_admin, creds));

  return 0;
}

// Sets up tables if they do not exist.
int BigtableStore::init_schema() {
  ClientContext get_ctx;

  GetTableRequest get_req;
  Table get_resp;
  get_req.set_name(table_name_);

  // if the table exists, do nothing
  Status get_s = admin_->GetTable(&get_ctx, get_req, &get_resp);
  if (get_s.ok())
    return 0;

  // otherwise, create the table
  LOG(INFO) << "Couldn't find table: " << get_s.error_message();
  LOG(INFO) << "Creating the observations table.";

  CreateTableRequest create_req;
  Table create_resp;

  // figure out the parent and table_id
  const char *table_name = table_name_.c_str();
  const char *p = strstr(table_name, "/tables/");

  if (!p) {
    LOG(ERROR) << "Bad table name: " << table_name_;
    return -1;
  }

  std::string parent(table_name, p - table_name);
  std::string table_id(p + 8);

  create_req.set_parent(parent);
  create_req.set_table_id(table_id);

  // setup columns.
  Table* create_req_tab = create_req.mutable_table();
  (*create_req_tab->mutable_column_families())["data"] = ColumnFamily();

  // do request.
  ClientContext create_ctx;
  Status create_s = admin_->CreateTable(&create_ctx, create_req, &create_resp);

  if (!create_s.ok()) {
    LOG(ERROR) << "Can't create table: " << create_s.error_message();
    return -1;
  }

  return 0;
}

int BigtableStore::put(const std::string& key, const std::string& val) {
  ClientContext context;
  MutateRowRequest req;
  MutateRowResponse resp;

  req.set_table_name(table_name_);
  req.set_row_key(key);

  Mutation_SetCell* cell = req.add_mutations()->mutable_set_cell();
  cell->set_family_name("data");
  cell->set_value(val);

  Status s = data_->MutateRow(&context, req, &resp);

  if (!s.ok()) {
    LOG(ERROR) << "Put failed: " << s.error_message();
    return -1;
  }

  return 0;
}

int BigtableStore::get(const std::string& key, std::string* out) {
  return -1;
}

int BigtableStore::get_range(const std::string& start, const std::string& end,
                             std::map<std::string, std::string>* out) {
  ClientContext context;
  ReadRowsRequest req;
  ReadRowsResponse resp;

  req.set_table_name(table_name_);

  RowSet* rs = req.mutable_rows();
  RowRange* rr = rs->add_row_ranges();

  if (start != "")
    rr->set_start_key_closed(start);

  if (end != "")
    rr->set_end_key_closed(end);

  auto reader(data_->ReadRows(&context, req));

  while (reader->Read(&resp)) {
    for (const ReadRowsResponse_CellChunk& chunk : resp.chunks()) {
      (*out)[chunk.row_key()] = chunk.value();
    }
  }

  Status s = reader->Finish();

  if (!s.ok()) {
    LOG(ERROR) << "get_range failed: " << s.error_message();
    return -1;
  }

  return 0;
}

}  // namespace analyzer
}  // namespace cobalt
