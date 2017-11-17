// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include "util/gcs/gcs_util.h"

#include "glog/logging.h"

#include "third_party/google-api-cpp-client/service_apis/storage/google/storage_api/storage_api.h"
#include "third_party/google-api-cpp-client/src/googleapis/client/auth/oauth2_authorization.h"
#include "third_party/google-api-cpp-client/src/googleapis/client/auth/oauth2_service_authorization.h"
#include "third_party/google-api-cpp-client/src/googleapis/client/data/data_reader.h"
#include "third_party/google-api-cpp-client/src/googleapis/client/transport/curl_http_transport.h"
#include "third_party/google-api-cpp-client/src/googleapis/client/transport/http_transport.h"
#include "third_party/google-api-cpp-client/src/googleapis/strings/stringpiece.h"

#include "util/pem_util.h"

using googleapis::client::CurlHttpTransportFactory;
using googleapis::client::HttpTransportLayerConfig;
using googleapis::client::NewUnmanagedInMemoryDataReader;
using googleapis::client::OAuth2Credential;
using googleapis::client::OAuth2ServiceAccountFlow;
using google_storage_api::BucketsResource_ListMethod;
using google_storage_api::ObjectsResource_InsertMethod;
using google_storage_api::StorageService;

namespace cobalt {
namespace util {
namespace gcs {

struct GcsUtil::Impl {
  googleapis::client::OAuth2Credential oauth_credential_;
  std::unique_ptr<google_storage_api::StorageService> storage_service_;
  std::unique_ptr<googleapis::client::OAuth2ServiceAccountFlow> oauth_flow_;
  std::unique_ptr<googleapis::client::HttpTransportLayerConfig> http_config_;
};

GcsUtil::GcsUtil() : impl_(new Impl()) {}

GcsUtil::~GcsUtil() {}

bool GcsUtil::InitFromDefaultPaths() {
  char* p = std::getenv("GRPC_DEFAULT_SSL_ROOTS_FILE_PATH");
  if (!p) {
    LOG(ERROR) << "The environment variable GRPC_DEFAULT_SSL_ROOTS_FILE_PATH "
                  "is not set.";
    return false;
  }
  std::string ca_certs_path(p);
  p = std::getenv("GOOGLE_APPLICATION_CREDENTIALS");
  if (!p) {
    LOG(ERROR) << "The environment variable GOOGLE_APPLICATION_CREDENTIALS "
                  "is not set.";
    return false;
  }
  std::string service_account_json_path(p);
  return Init(ca_certs_path, service_account_json_path);
}

bool GcsUtil::Init(const std::string ca_certs_path,
                   const std::string& service_account_json_path) {
  // Set up HttpTransportLayer.
  impl_->http_config_.reset(new HttpTransportLayerConfig);
  impl_->http_config_->ResetDefaultTransportFactory(
      new CurlHttpTransportFactory(impl_->http_config_.get()));
  impl_->http_config_->mutable_default_transport_options()->set_cacerts_path(
      ca_certs_path);

  // Set up OAuth 2.0 flow for a service account.
  googleapis::util::Status status;
  impl_->oauth_flow_.reset(new OAuth2ServiceAccountFlow(
      impl_->http_config_->NewDefaultTransport(&status)));
  if (!status.ok()) {
    LOG(ERROR) << "GcsUitl::Init(). Error creating new Http transport: "
               << status.ToString();
    return false;
  }

  // Load the the contents of the service account json into a string.
  std::string json;
  PemUtil::ReadTextFile(service_account_json_path, &json);
  if (json.empty()) {
    LOG(ERROR) << "GcsUitl::Init(). Unable to read service account json from: "
               << service_account_json_path;
    return false;
  }
  // Initialize the flow with the contents of the service account json.
  impl_->oauth_flow_->InitFromJson(json);
  impl_->oauth_flow_->set_default_scopes(
      StorageService::SCOPES::DEVSTORAGE_READ_WRITE);
  // Connect the credential with the AuthFlow.
  impl_->oauth_credential_.set_flow(impl_->oauth_flow_.get());

  // Construct the storage service.
  impl_->storage_service_.reset(
      new StorageService(impl_->http_config_->NewDefaultTransport(&status)));
  if (!status.ok()) {
    LOG(ERROR) << "GcsUitl::Init(). Error creating new Http transport: "
               << status.ToString();
    return false;
  }

  return true;
}

bool GcsUtil::Upload(const std::string& bucket, const std::string& path,
                     const std::string mime_type, const char* data,
                     size_t num_bytes) {
  // Build the request.
  googleapis::StringPiece str;
  str.set(data, num_bytes);
  std::unique_ptr<ObjectsResource_InsertMethod> request(
      impl_->storage_service_->get_objects().NewInsertMethod(
          &(impl_->oauth_credential_), bucket, nullptr, mime_type.c_str(),
          NewUnmanagedInMemoryDataReader(str)));
  request->set_name(path);

  // Execute the request.
  Json::Value value;
  google_storage_api::Object response(&value);
  auto status = request->ExecuteAndParseResponse(&response);
  if (status.ok()) {
    return true;
  }
  LOG(ERROR) << "Error attempting upload: " << status.ToString();
  return false;
}

bool GcsUtil::Ping(std::string project_id) {
  // Construct the request.
  google_storage_api::BucketsResource_ListMethod request(
      impl_->storage_service_.get(), &(impl_->oauth_credential_), project_id);
  Json::Value value;
  google_storage_api::Buckets buckets(&value);
  // Execute the request.
  auto status = request.ExecuteAndParseResponse(&buckets);
  if (status.ok()) {
    return true;
  }
  LOG(ERROR) << "Error attempting to list buckets: " << status.ToString();
  return false;
}

}  // namespace gcs
}  // namespace util
}  // namespace cobalt
