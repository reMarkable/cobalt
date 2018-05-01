// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_UTIL_STATUS_H_
#define COBALT_UTIL_STATUS_H_

#include "util/status_codes.h"

#include <string>

namespace cobalt {
namespace util {

class Status {
 public:
  Status() : code_(StatusCode::OK) {}

  Status(StatusCode code, const std::string &error_message)
      : code_(code), error_message_(error_message) {}

  Status(StatusCode code, const std::string &error_message,
         const std::string &error_details)
      : code_(code),
        error_message_(error_message),
        error_details_(error_details) {}

  // Pre-defined special status objects.
  static const Status &OK;
  static const Status &CANCELLED;

  StatusCode error_code() const { return code_; }
  std::string error_message() const { return error_message_; }
  std::string error_details() const { return error_details_; }

  bool ok() const { return code_ == StatusCode::OK; }

  // Ignores any errors. This method does nothing except potentially suppress
  // complaints from any tools that are checking that errors are not dropped on
  // the floor.
  void IgnoreError() {}

 private:
  StatusCode code_;
  std::string error_message_;
  std::string error_details_;
};

// Early-returns the status if it is an error, otherwise it proceeds.
//
// The argument expression is evaluated only once.
#define RETURN_IF_ERROR(__status) \
  do {                            \
    auto status = (__status);     \
    if (!status.ok()) {           \
      return status;              \
    }                             \
  } while (false)

}  // namespace util
}  // namespace cobalt

#endif  // COBALT_UTIL_STATUS_H_
