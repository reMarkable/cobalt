# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Includes cobalt specific clearcut implementations.

# curl_http_client implements http_client for linux client (not intended for use
# in fuchsia).
add_library(curl_http_client curl_http_client.cc curl_handle.cc)
target_link_libraries(curl_http_client clearcut curl)
