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

#include <string>

using google::bigtable::v2::Bigtable;
using google::bigtable::v2::MutateRowRequest;
using google::bigtable::v2::MutateRowResponse;
using google::bigtable::v2::Mutation_SetCell;
using grpc::ClientContext;
using grpc::Status;

namespace cobalt {
namespace analyzer {

int BigtableStore::initialize(const std::string& table_name) {
  // If running outside of GCE, set the GOOGLE_APPLICATION_CREDENTIALS
  // environment variable.  Otherwise you'll hit assertions.
  auto creds = grpc::GoogleDefaultCredentials();
  auto chan = grpc::CreateChannel("bigtable.googleapis.com", creds);

  bigtable_ = Bigtable::NewStub(chan);
  table_name_ = table_name;

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

  Status s = bigtable_->MutateRow(&context, req, &resp);

  if (!s.ok()) {
    LOG(ERROR) << "ERR " << s.error_message();
    return -1;
  }

  return 0;
}

int BigtableStore::get(const std::string& key, std::string* out) {
  return -1;
}

}  // namespace analyzer
}  // namespace cobalt
