// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_CLEARCUT_HTTP_CLIENT_H_
#define THIRD_PARTY_CLEARCUT_HTTP_CLIENT_H_

#include <future>
#include <map>

#include "third_party/tensorflow_statusor/statusor.h"
#include "util/status.h"

namespace clearcut {

using cobalt::util::Status;
using tensorflow_statusor::StatusOr;

struct HTTPResponse {
  std::string response;
  Status status;
  int64_t http_code;
};

struct HTTPRequest {
  std::string body;
  std::string url;
  std::map<std::string, std::string> headers;
};

class HTTPClient {
 public:
  // Post an HTTPRequest which will timeout after |timeout_ms| milliseconds.
  virtual std::future<StatusOr<HTTPResponse>> Post(
      HTTPRequest request, std::chrono::steady_clock::time_point deadline) = 0;

  virtual ~HTTPClient() = default;
};

}  // namespace clearcut

#endif  // THIRD_PARTY_CLEARCUT_HTTP_CLIENT_H_
