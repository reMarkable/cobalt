/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "third_party/tensorflow_statusor/statusor.h"
#include "glog/logging.h"
#include "grpc++/grpc++.h"

namespace tensorflow_statusor {
namespace internal_statusor {

using grpc::Status;

void Helper::HandleInvalidStatusCtorArg(Status* status) {
  const char* kMessage =
      "An OK status is not a valid constructor argument to StatusOr<T>";
  LOG(ERROR) << kMessage;
  // Fall back to grpc::StatusCode::INTERNAL.
  *status = grpc::Status(grpc::StatusCode::INTERNAL, kMessage);
}

void Helper::Crash(const Status& status) {
  LOG(FATAL) << "Attempting to fetch value instead of handling error "
             << status.error_message();
}

}  // namespace internal_statusor
}  // namespace tensorflow_statusor
