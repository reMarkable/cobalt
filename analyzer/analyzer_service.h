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

#include <memory>
#include <string>
#include <utility>

#include "analyzer/analyzer.grpc.pb.h"
#include "analyzer/store/data_store.h"
#include "analyzer/store/observation_store.h"
#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {

// Implements the Analyzer gRPC service.  It will receive observations via gRPC
// and store them in Bigtable.  No analysis is performed.  Analysis is
// kicked-off and done by other components (i.e., the reporter)
class AnalyzerServiceImpl final : public Analyzer::Service {
 public:
  static std::unique_ptr<AnalyzerServiceImpl> CreateFromFlagsOrDie();

  // Constructs an AnalyzerServiceImpl that accessess the given
  // |observation_store|, listens on the given tcp |port|, and uses
  // the given |server_credentials| for authentication and encryption.
  AnalyzerServiceImpl(
      std::shared_ptr<store::ObservationStore> observation_store, int port,
      std::shared_ptr<grpc::ServerCredentials> server_credentials);

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
  // Decrypts the |ciphertext| in |em| and then parses the resulting bytes
  // as an Observation and writes the result into |observation|. Returns OK
  // if this succeeds or an error Status containing an appropriate error
  // message otherwise.
  grpc::Status ParseEncryptedObservation(Observation* observation,
                                 const EncryptedMessage& em);

  std::shared_ptr<store::ObservationStore> observation_store_;
  int port_;
  std::shared_ptr<grpc::ServerCredentials> server_credentials_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_ANALYZER_SERVICE_H_
