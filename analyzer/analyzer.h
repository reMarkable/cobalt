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

#ifndef COBALT_ANALYZER_ANALYZER_H_
#define COBALT_ANALYZER_ANALYZER_H_

#include <grpc++/grpc++.h>

#include <memory>

#include "analyzer/analyzer.grpc.pb.h"

namespace cobalt {
namespace analyzer {

const int kAnalyzerPort = 8080;

// Main analyzer class
class AnalyzerServiceImpl final : public Analyzer::Service {
 public:
  // Starts the analyzer service
  void Start();

  // Stops the analyzer service
  void Shutdown();

  // Waits for the analyzer service to terminate.  Shutdown() must be called for
  // Wait() to return.
  void Wait();

  // Shuffler -> Analyzer entry point
  grpc::Status AddObservations(grpc::ServerContext* context,
                               const ObservationBatch* request,
                               google::protobuf::Empty* response) override;

 private:
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_ANALYZER_H_
