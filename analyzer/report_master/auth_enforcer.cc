// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/auth_enforcer.h"

#include <string>

#include "glog/logging.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "util/crypto_util/base64.h"

DEFINE_bool(googlers_only, false,
            "Should only Googlers be able to access the ReportMaster Service? "
            "Default=false. (Note that this assumes ReportMaster Service is "
            "protected by Google Cloud Endpoints which performs the "
            "authentication.");
DEFINE_bool(authorization_log_only, false,
            "If this flag is true, whenever a request would not be authorized, "
            "it is allowed to go through, but a log line is generated to "
            "indicate a request would have failed but for this flag. "
            "Default=false.");

namespace cobalt {
namespace analyzer {

std::shared_ptr<AuthEnforcer> AuthEnforcer::CreateFromFlagsOrDie() {
  std::shared_ptr<AuthEnforcer> enforcer;

  if (FLAGS_googlers_only) {
    enforcer.reset(new GoogleEmailEnforcer());
  } else {
    enforcer.reset(new NullEnforcer());
  }

  if (FLAGS_authorization_log_only) {
    enforcer.reset(new LogOnlyEnforcer(enforcer));
  }
  return enforcer;
}

// When Google Cloud Endpoints authenticates a gRPC request, it appends a
// field to the request metadata with the authenticated user's info.
const char kUserInfoKey[] = "x-endpoint-api-userinfo";

// Extracts the email address of the authenticated user from the base64 encoded
// user info provided by Google Cloud Endpoint.
// This function is separated from GetEmailFromServerContext for testing.
grpc::Status GoogleEmailEnforcer::GetEmailFromEncodedUserInfo(
    const std::string &encoded_user_info, std::string *email) {
  grpc::Status could_not_authorize(grpc::StatusCode::PERMISSION_DENIED,
                                   "Could not authorize the user.");

  std::string decoded_user_info;
  if (!cobalt::crypto::Base64Decode(encoded_user_info, &decoded_user_info)) {
    LOG(ERROR) << "User info could not be decoded: " << encoded_user_info;
    return could_not_authorize;
  }

  rapidjson::Document user_info_doc;
  user_info_doc.Parse(decoded_user_info.c_str());

  if (user_info_doc.HasParseError() || !user_info_doc.IsObject() ||
      !user_info_doc.HasMember("email") || !user_info_doc["email"].IsString()) {
    LOG(ERROR) << "Could not get email from user info: " << decoded_user_info;
    return could_not_authorize;
  }

  *email = user_info_doc["email"].GetString();
  return grpc::Status::OK;
}

// Extracts the email address of the authenticated user from the metadata
// provided by Google Cloud Endpoints.
grpc::Status GoogleEmailEnforcer::GetEmailFromServerContext(
    grpc::ServerContext *context, std::string *email) {
  auto client_metadata = context->client_metadata();
  auto user_info = client_metadata.find(kUserInfoKey);

  if (user_info == client_metadata.end()) {
    return grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                        "Call to the Report Master was not authenticated.");
  }

  std::string encoded_user_info(user_info->second.data(),
                                user_info->second.size());

  return GetEmailFromEncodedUserInfo(encoded_user_info, email);
}

grpc::Status NullEnforcer::CheckAuthorization(grpc::ServerContext *context,
                                              uint32_t customer_id,
                                              uint32_t project_id,
                                              uint32_t report_config_id) {
  return grpc::Status::OK;
}

grpc::Status NegativeEnforcer::CheckAuthorization(grpc::ServerContext *context,
                                                  uint32_t customer_id,
                                                  uint32_t project_id,
                                                  uint32_t report_config_id) {
  return grpc::Status(grpc::StatusCode::PERMISSION_DENIED,
                      "All requests are denied.");
}

grpc::Status GoogleEmailEnforcer::CheckAuthorization(
    grpc::ServerContext *context, uint32_t customer_id, uint32_t project_id,
    uint32_t report_config_id) {
  std::string email;
  auto status = GetEmailFromServerContext(context, &email);
  if (!status.ok()) {
    return status;
  }

  if (!CheckGoogleEmail(email)) {
    LOG(INFO) << "Rejected attempt to use the API by: " << email;
    return grpc::Status(grpc::StatusCode::PERMISSION_DENIED,
                        "This deployment of the report master requires "
                        "google.com credentials.");
  }

  return grpc::Status::OK;
}

// Checks that this is a valid google.com email address.
bool GoogleEmailEnforcer::CheckGoogleEmail(std::string email) {
  size_t at = email.find_first_of('@');
  if (at == std::string::npos) {
    return false;
  }

  // Usernames must be no more than 14 letters long.
  if (at > 14) {
    return false;
  }

  // Usernames must be composed of lower case letters only.
  for (size_t i = 0; i < at; i++) {
    char c = email[i];
    if (c > 'z' || c < 'a') {
      return false;
    }
  }

  // Only google.com email addresses.
  if (email.substr(at).compare("@google.com") != 0) {
    return false;
  }

  return true;
}

LogOnlyEnforcer::LogOnlyEnforcer(std::shared_ptr<AuthEnforcer> enforcer)
    : enforcer_(enforcer) {}

grpc::Status LogOnlyEnforcer::CheckAuthorization(grpc::ServerContext *context,
                                                 uint32_t customer_id,
                                                 uint32_t project_id,
                                                 uint32_t report_config_id) {
  grpc::Status status = enforcer_->CheckAuthorization(
      context, customer_id, project_id, report_config_id);
  if (!status.ok()) {
    LOG(INFO) << "Request would have failed with: " << status.error_code()
              << ": " << status.error_message();
  }

  return grpc::Status::OK;
}

}  // namespace analyzer
}  // namespace cobalt
