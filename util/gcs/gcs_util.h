// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_UTIL_GCS_GCS_UTIL_H_
#define COBALT_UTIL_GCS_GCS_UTIL_H_

#include <memory>
#include <string>
#include <vector>

#include "third_party/google-api-cpp-client/service_apis/storage/google/storage_api/storage_api.h"
#include "third_party/google-api-cpp-client/src/googleapis/client/auth/oauth2_authorization.h"
#include "third_party/google-api-cpp-client/src/googleapis/client/auth/oauth2_service_authorization.h"
#include "third_party/google-api-cpp-client/src/googleapis/client/transport/http_transport.h"

namespace cobalt {
namespace util {
namespace gcs {

// A utility for uploading files to Google Cloud Storage.
//
// Usage:
//   - construct an instance of GcsUtil
//   - Invoke one of the two Init..() methods.
//   - Repeatedly invoke Upload() to upload files.
//
// The implementation uses the library in google-api-cpp-client which in
// in turn uses libcurl for HTTP transport. From experimentation it appears
// that often times the first upload after the instance is created will fail
// with a timeout. We do not understand why. Subsequent uploads succeed. One
// workaround is to invoke the Ping() method prior to the first upload. This
// appears to succeed. We do not understand why. See
// https://github.com/google/google-api-cpp-client/issues/48
class GcsUtil {
 public:
  // Initializes this instance using default file paths for the CA root
  // certificates and service account json.
  //
  // The path to the CA root cert file is read from the environment varialbe
  // "GRPC_DEFAULT_SSL_ROOTS_FILE_PATH". The path to the service account
  // json file is read from teh environment variable
  // "GOOGLE_APPLICATION_CREDENTIALS".
  //
  // Returns true on success. On failure, logs an Error and returns false.
  // If this method returns false, do not continue to use this instance.
  // Discard this instance and try again.
  bool InitFromDefaultPaths();

  // Initializes this instance by reading the CA root certificates and
  // service account json from the given paths.
  //
  // Returns true on success. On failure, logs an Error and returns false.
  // If this method returns false, do not continue to use this instance.
  // Discard this instance and try again.
  //
  // ca_certs_path. The path to a file containing a PEM encoding of CA root
  // certificates.
  //
  // service_account_json_path. The path to a json file containg a Google
  // service account private key.
  bool Init(const std::string ca_certs_path,
            const std::string& service_account_json_path);

  // Uploads a blob to Google Cloud Storage. |num_bytes| from |data| are
  // uploaded at the given |path| in the given |bucket|. This will succeed only
  // if the service account specified when this instance was initalized has
  // write permission on the bucket. Returns true on success. On failure,
  // logs an Error and returns false.
  bool Upload(const std::string& bucket, const std::string& path,
              const std::string mime_type, const char* data, size_t num_bytes);

  // Attempts to connect with Google Cloud Storage and query for the list
  // of buckets in the specified project. This will succeed only if the service
  // account specified when this instance was initialized has read permission
  // on the bucket. Returns true on success. On failure, logs an Error and
  // returns.
  //
  // NOTE: For reasons not fully understood, it appears it is sometimes
  // necessary to perform a Ping() prior to the first Upload. In testing we
  // have observed that Upload will timeout unless at least one Ping() is
  // performed first. We do not understand why.
  bool Ping(std::string project_id);

 private:
  googleapis::client::OAuth2Credential oauth_credential_;
  std::unique_ptr<google_storage_api::StorageService> storage_service_;
  std::unique_ptr<googleapis::client::OAuth2ServiceAccountFlow> oauth_flow_;
  std::unique_ptr<googleapis::client::HttpTransportLayerConfig> http_config_;
};

}  // namespace gcs
}  // namespace util
}  // namespace cobalt

#endif  // COBALT_UTIL_GCS_GCS_UTIL_H_
