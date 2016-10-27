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

#ifndef COBALT_ANALYZER_ANALYZER_SERVICE_H_
#define COBALT_ANALYZER_ANALYZER_SERVICE_H_

#include <grpc++/grpc++.h>

#include <memory>
#include <string>
#include <utility>

#include "analyzer/analyzer.grpc.pb.h"
#include "analyzer/store/store.h"

namespace cobalt {
namespace analyzer {

const int kAnalyzerPort = 8080;

// Implements the Analyzer gRPC service.  It will receive observations via gRPC
// and store them in Bigtable.  No analysis is performed.  Analysis is
// kicked-off and done by other components (i.e., the reporter)
class AnalyzerServiceImpl final : public Analyzer::Service {
 public:
  explicit AnalyzerServiceImpl(std::unique_ptr<Store>&& store)
      : store_(std::move(store)) {}

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
  // The row key is currently defined as:
  // customer:project:metric:day:random
  // Random is 64bit to try and avoid collisions on observations for the same
  // day.
  std::string make_row_key(const ObservationMetadata& metadata);

  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<Store> store_;
};

// This is the main method for the analyzer service.  This call blocks forever.
// Currently it is not folded into main() because we run both the
// analyzer_service and the reporter in a single process and each have their own
// "main()".
void analyzer_service_main();

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_ANALYZER_SERVICE_H_
