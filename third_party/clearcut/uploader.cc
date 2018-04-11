// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cmath>

#include "glog/logging.h"
#include "third_party/clearcut/clearcut.pb.h"
#include "third_party/clearcut/uploader.h"
#include "third_party/tensorflow_statusor/status_macros.h"
#include "unistd.h"

namespace clearcut {

using cobalt::util::Status;
using cobalt::util::StatusCode;

ClearcutUploader::ClearcutUploader(const std::string& url,
                                   std::unique_ptr<HTTPClient> client,
                                   int64_t upload_timeout)
    : url_(url),
      client_(std::move(client)),
      upload_timeout_(upload_timeout),
      pause_uploads_until_(
          std::chrono::steady_clock::now())  // Set this to now() so that we
                                             // can immediately upload.
{}

Status ClearcutUploader::UploadEvents(LogRequest* log_request,
                                      int32_t max_retries) {
  int32_t i = 0;
  auto deadline = std::chrono::steady_clock::time_point::max();
  if (upload_timeout_ > 0) {
    deadline = std::chrono::steady_clock::now() +
               std::chrono::milliseconds(upload_timeout_);
  }
  auto backoff = std::chrono::milliseconds(250);
  while (true) {
    Status response = TryUploadEvents(log_request, deadline);
    if (response.ok() || ++i == max_retries) {
      return response;
    }
    switch (response.error_code()) {
      case StatusCode::INVALID_ARGUMENT:
      case StatusCode::NOT_FOUND:
      case StatusCode::PERMISSION_DENIED:
        // Don't retry permanent errors.
        LOG(WARNING) << "Got a permanent error from TryUploadEvents: "
                     << response.error_message();
        return response;
      default:
        break;
    }
    if (std::chrono::steady_clock::now() > deadline) {
      return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded.");
    }
    // Exponential backoff.
    auto time_until_pause_end =
        pause_uploads_until_ - std::chrono::steady_clock::now();
    if (time_until_pause_end > backoff) {
      std::this_thread::sleep_for(time_until_pause_end);
    } else {
      std::this_thread::sleep_for(backoff);
    }
    backoff *= 2;
  }
}

Status ClearcutUploader::TryUploadEvents(
    LogRequest* log_request, std::chrono::steady_clock::time_point deadline) {
  if (std::chrono::steady_clock::now() < pause_uploads_until_) {
    return Status(StatusCode::RESOURCE_EXHAUSTED,
                  "Uploads are currently paused at the request of the "
                  "clearcut server");
  }

  HTTPRequest request = {.url = url_};
  log_request->mutable_client_info()->set_client_type(kFuchsiaClientType);
  log_request->SerializeToString(&request.body);
  auto response_future = client_->Post(request, deadline);
  auto response_or = response_future.get();
  if (!response_or.ok()) {
    return response_or.status();
  }

  auto response = response_or.ConsumeValueOrDie();

  std::ostringstream s;
  s << response.http_code << ": ";
  switch (response.http_code) {
    case 200:  // success
      break;
    case 400:  // bad request
      s << "Bad Request";
      return Status(StatusCode::INVALID_ARGUMENT, s.str());
    case 401:  // Unauthorized
    case 403:  // forbidden
      s << "Permission Denied";
      return Status(StatusCode::PERMISSION_DENIED, s.str());
    case 404:  // not found
      s << "Not Found";
      return Status(StatusCode::NOT_FOUND, s.str());
    case 503:  // service unavailable
      s << "Service Unavailable";
      return Status(StatusCode::RESOURCE_EXHAUSTED, s.str());
    default:
      s << "Unknown Error Code";
      return Status(StatusCode::UNKNOWN, s.str());
  }

  LogResponse log_response;
  if (!log_response.ParseFromString(response.response)) {
    return Status(StatusCode::INTERNAL,
                  "Unable to parse response from clearcut server");
  }

  if (log_response.next_request_wait_millis() >= 0) {
    pause_uploads_until_ =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(log_response.next_request_wait_millis());
  }

  return Status::OK;
}

}  // namespace clearcut
