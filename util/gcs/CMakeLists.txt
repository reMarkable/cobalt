# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A utility library for interacting with Google Cloud Storage

add_compile_options(-Wno-unused-local-typedef)
add_library(gcs_util
            gcs_util.cc)
target_link_libraries(gcs_util
                      pem_util
                      google_storage_api
                      googleapis_curl_http
                      googleapis_http
                      googleapis_json
                      googleapis_jsoncpp
                      googleapis_oauth2
                      googleapis_utils
                      googleapis_openssl_codec
                      googleapis_scribes
                      googleapis_http
                      googleapis_internal
                      jsoncpp
                      curl)
add_cobalt_dependencies(gcs_util)

add_executable(gcs_util_tests
               gcs_util_test.cc)
target_link_libraries(gcs_util_tests
                      gcs_util)
add_cobalt_test_dependencies(gcs_util_tests
                             ${DIR_GTESTS})
