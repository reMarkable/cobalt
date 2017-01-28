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

#include "analyzer/store/bigtable_admin.h"

#include <glog/logging.h>
#include <google/bigtable/admin/v2/bigtable_table_admin.grpc.pb.h>

#include <utility>

#include "analyzer/store/bigtable_flags.h"
#include "analyzer/store/bigtable_names.h"

namespace cobalt {
namespace analyzer {
namespace store {

using google::bigtable::admin::v2::BigtableTableAdmin;
using google::bigtable::admin::v2::ColumnFamily;
using google::bigtable::admin::v2::CreateTableRequest;
using google::bigtable::admin::v2::GetTableRequest;
using grpc::ClientContext;

typedef google::bigtable::admin::v2::Table BtTable;

std::shared_ptr<BigtableAdmin> BigtableAdmin::CreateFromFlagsOrDie() {
  // See https://developers.google.com/identity/protocols/ \
  //         application-default-credentials
  // for an explanation of grpc::GoogleDefaultCredentials(). When running
  // on GKE this should cause the service account to be used. When running
  // on a developer's machine this might either use the user's oauth credentials
  // or a service account if the user has installed one. To use a service
  // account the library looks for a key file located at the path specified in
  // the environment variable GOOGLE_APPLICATION_CREDENTIALS.
  CHECK_NE("", FLAGS_bigtable_project_name);
  CHECK_NE("", FLAGS_bigtable_instance_name);
  auto creds = grpc::GoogleDefaultCredentials();
  CHECK(creds);
  LOG(INFO) << "Connecting to CloudBigtable admin API at "
            << kCloudBigtableAdminUri;
  return std::shared_ptr<BigtableAdmin>(new BigtableAdmin(
      kCloudBigtableAdminUri, creds, FLAGS_bigtable_project_name,
      FLAGS_bigtable_instance_name));
}

BigtableAdmin::BigtableAdmin(
    const std::string& uri,
    std::shared_ptr<grpc::ChannelCredentials> credentials,
    std::string project_name, std::string instance_name)
    : channel_(grpc::CreateChannel(uri, credentials)),
      stub_(BigtableTableAdmin::NewStub(channel_)),
      project_name_(std::move(project_name)),
      instance_name_(std::move(instance_name)) {}

bool BigtableAdmin::WaitForConnected(
    std::chrono::system_clock::time_point deadline) {
  return channel_->WaitForConnected(deadline);
}

bool BigtableAdmin::CreateTablesIfNecessary() {
  return CreateTableIfNecessary(kObservationsTableId) &&
         CreateTableIfNecessary(kReportMetadataTableId) &&
         CreateTableIfNecessary(kReportRowsTableId);
}

bool BigtableAdmin::CreateTableIfNecessary(std::string table_id) {
  ClientContext get_ctx;

  GetTableRequest get_req;
  BtTable get_resp;
  get_req.set_name(
      BigtableNames::FullTableName(project_name_, instance_name_, table_id));

  // If the table exists, do nothing.
  grpc::Status get_s = stub_->GetTable(&get_ctx, get_req, &get_resp);
  if (get_s.ok()) {
    return true;
  }

  // Otherwise, create the table.
  CreateTableRequest create_req;
  BtTable create_resp;

  create_req.set_parent(
      BigtableNames::TableParentName(project_name_, instance_name_));
  create_req.set_table_id(std::move(table_id));

  // Set up columns.
  BtTable* create_req_tab = create_req.mutable_table();
  (*create_req_tab->mutable_column_families())[kDataColumnFamilyName] =
      ColumnFamily();

  // Do the request.
  ClientContext create_ctx;
  grpc::Status create_s =
      stub_->CreateTable(&create_ctx, create_req, &create_resp);

  if (!create_s.ok() && create_s.error_code() != grpc::ALREADY_EXISTS) {
    std::string error_message = create_s.error_message();
    // In practice it appears that the Bigtable Emulator does not in fact
    // return the appropriate error code, ALREADY_EXISTS, but rather returns
    // UNKNOWN. We found that we are able to detect the problem in this case
    // by looking for the text 'already exists'.
    if (error_message.find("already exists") == -1) {
      LOG(ERROR) << "Can't create table: " << create_s.error_message()
                 << " error code=" << create_s.error_code();
      return false;
    }
  }
  return true;
}

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
