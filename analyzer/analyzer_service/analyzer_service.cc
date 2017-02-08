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

#include "analyzer/analyzer_service/analyzer_service.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <string>

#include "./observation.pb.h"
#include "analyzer/store/data_store.h"

namespace cobalt {
namespace analyzer {

using store::DataStore;
using store::ObservationStore;

DEFINE_int32(port, 0, "The port that the Analyzer Service should listen on.");
DEFINE_string(ssl_cert_info, "", "TBD: Some info about SSL Certificates.");

std::unique_ptr<AnalyzerServiceImpl>
AnalyzerServiceImpl::CreateFromFlagsOrDie() {
  std::shared_ptr<DataStore> data_store(
      DataStore::CreateFromFlagsOrDie().release());
  std::shared_ptr<ObservationStore> observation_store(
      new ObservationStore(data_store));
  CHECK(FLAGS_port) << "--port is a mandatory flag";
  std::shared_ptr<grpc::ServerCredentials> server_credentials;
  if (FLAGS_ssl_cert_info.empty()) {
    LOG(WARNING) << "WARNING: Using insecure server credentials. Pass "
                    "-ssl_cert_info to enable SSL.";
    server_credentials = grpc::InsecureServerCredentials();
  } else {
    LOG(INFO) << "Reading SSL certificate information from '"
              << FLAGS_ssl_cert_info << "'.";
    grpc::SslServerCredentialsOptions options;
    // TODO(rudominer) Set up options based on FLAGS_ssl_cert_info.
    server_credentials = grpc::SslServerCredentials(options);
  }
  return std::unique_ptr<AnalyzerServiceImpl>(new AnalyzerServiceImpl(
      observation_store, FLAGS_port, server_credentials));
}

AnalyzerServiceImpl::AnalyzerServiceImpl(
    std::shared_ptr<store::ObservationStore> observation_store, int port,
    std::shared_ptr<grpc::ServerCredentials> server_credentials)
    : observation_store_(observation_store),
      port_(port),
      server_credentials_(server_credentials) {}

void AnalyzerServiceImpl::Start() {
  grpc::ServerBuilder builder;
  char local_address[1024];
  // We use 0.0.0.0 to indicate the wildcard interface.
  snprintf(local_address, sizeof(local_address), "0.0.0.0:%d", port_);
  builder.AddListeningPort(local_address, server_credentials_);
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  LOG(INFO) << "Starting Analyzer service on port " << port_;
}

void AnalyzerServiceImpl::Shutdown() { server_->Shutdown(); }

void AnalyzerServiceImpl::Wait() { server_->Wait(); }

grpc::Status AnalyzerServiceImpl::AddObservations(
    grpc::ServerContext* context, const ObservationBatch* batch,
    google::protobuf::Empty* empty) {
  for (const EncryptedMessage& em : batch->encrypted_observation()) {
    Observation observation;
    auto parse_status = ParseEncryptedObservation(&observation, em);
    if (!parse_status.ok()) {
      return parse_status;
    }
    auto add_status =
        observation_store_->AddObservation(batch->meta_data(), observation);
    if (add_status != store::kOK) {
      LOG(ERROR) << "AddObservations() failed with status code " << add_status;
      switch (add_status) {
        case store::kInvalidArguments:
          return grpc::Status(grpc::INVALID_ARGUMENT, "");

        default:
          return grpc::Status(grpc::INTERNAL, "");
      }
    }
  }

  return grpc::Status::OK;
}

grpc::Status AnalyzerServiceImpl::ParseEncryptedObservation(
    Observation* observation, const EncryptedMessage& em) {
  // TODO(rudominer) Actually perform a decryption when bytes is encrypted.
  if (!observation->ParseFromString(em.ciphertext())) {
    std::string error_message(
        "An EncryptedMessage was successfully decrypted using "
        "the public key of the Analyzer, but the contents of "
        "the decrypted message could not be parsed as an "
        "Observation.");
    LOG(ERROR) << error_message;
    return grpc::Status(grpc::INVALID_ARGUMENT, error_message);
  }
  return grpc::Status::OK;
}

}  // namespace analyzer
}  // namespace cobalt
