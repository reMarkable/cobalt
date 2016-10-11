// Copyright 2016 The Fuchsia Authors
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

// The analyzer collector process receives reports via gRPC and stores them
// persistently.

#include "analyzer/analyzer.h"

#include <stdio.h>

using google::protobuf::Empty;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

namespace cobalt {
namespace analyzer {

Status AnalyzerServiceImpl::AddObservations(ServerContext* context,
                                            const ObservationBatch* request,
                                            Empty* response) {
  return Status::OK;
}

void AnalyzerServiceImpl::Start() {
  ServerBuilder builder;
  char port[1024];

  snprintf(port, sizeof(port), "0.0.0.0:%d", kAnalyzerPort);

  builder.AddListeningPort(port, grpc::InsecureServerCredentials());
  builder.RegisterService(this);

  server_ = builder.BuildAndStart();
}

void AnalyzerServiceImpl::Shutdown() {
  server_->Shutdown();
}

void AnalyzerServiceImpl::Wait() {
  server_->Wait();
}

}  // namespace analyzer
}  // namespace cobalt
