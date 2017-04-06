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

#ifndef COBALT_ANALYZER_ANALYZER_SERVICE_ANALYZER_SERVICE_H_
#define COBALT_ANALYZER_ANALYZER_SERVICE_ANALYZER_SERVICE_H_

#include <memory>
#include <string>
#include <utility>

#include "analyzer/analyzer_service/analyzer.grpc.pb.h"
#include "analyzer/store/data_store.h"
#include "analyzer/store/observation_store.h"
#include "grpc++/grpc++.h"
#include "util/encrypted_message_util.h"

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
  // the given TLS |server_credentials|.
  //
  // |private_key_pem| is the PEM encoding of the Analyzer's private key used
  // with Cobalt's encryption scheme in which the Encoder encrypts Observations
  // before sending them to the Shuffler. The Encoder must encrypt Observations
  // using the corresponding public key. This parameter may be set to the empty
  // string in which case the Analyzer will still function perfectly except
  // that it will only be able to consume Observations that are contained in
  // EncryptedMessages that uses the EncryptedMessage::NONE scheme, i.e.
  // Observations that are sent in plain text. This is useful for testing but
  // should never be done in a production Cobalt environment.
  AnalyzerServiceImpl(
      std::shared_ptr<store::ObservationStore> observation_store, int port,
      std::shared_ptr<grpc::ServerCredentials> server_credentials,
      const std::string& private_key_pem);

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
  std::shared_ptr<store::ObservationStore> observation_store_;
  int port_;
  std::shared_ptr<grpc::ServerCredentials> server_credentials_;
  std::unique_ptr<grpc::Server> server_;
  util::MessageDecrypter message_decrypter_;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_ANALYZER_SERVICE_ANALYZER_SERVICE_H_
