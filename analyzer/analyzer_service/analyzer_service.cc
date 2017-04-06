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
#include "util/encrypted_message_util.h"
#include "util/pem_util.h"

namespace cobalt {
namespace analyzer {

using store::DataStore;
using store::ObservationStore;
using util::MessageDecrypter;
using util::PemUtil;

DEFINE_int32(port, 0, "The port that the Analyzer Service should listen on.");
DEFINE_string(tls_info, "", "TBD: Some info about TLS.");
DEFINE_string(
    private_key_pem_file, "",
    "Path to a file containing a PEM encoding of the private key of "
    "the Analyzer used for Cobalt's internal encryption scheme. If "
    "not specified then the Analyzer will not support encrypted Observations.");

std::unique_ptr<AnalyzerServiceImpl>
AnalyzerServiceImpl::CreateFromFlagsOrDie() {
  std::shared_ptr<DataStore> data_store(
      DataStore::CreateFromFlagsOrDie().release());
  std::shared_ptr<ObservationStore> observation_store(
      new ObservationStore(data_store));
  CHECK(FLAGS_port) << "--port is a mandatory flag";
  std::shared_ptr<grpc::ServerCredentials> server_credentials;
  if (FLAGS_tls_info.empty()) {
    LOG(WARNING) << "WARNING: Using insecure server credentials. Pass "
                    "-tls_info to enable TLS.";
    server_credentials = grpc::InsecureServerCredentials();
  } else {
    grpc::SslServerCredentialsOptions options;
    // TODO(rudominer) Set up options based on FLAGS_tls_info.
    server_credentials = grpc::SslServerCredentials(options);
  }
  std::string private_key_pem;
  PemUtil::ReadTextFile(FLAGS_private_key_pem_file, &private_key_pem);
  if (private_key_pem.empty()) {
    LOG(WARNING) << "WARNING: No valid private key PEM was read. The Analyzer "
                    "will not be able to decrypt encrypted Observations.";
    LOG(WARNING) << "-private_key_pem_file=" << FLAGS_private_key_pem_file;
  } else {
    LOG(INFO) << "Analyzer private key was read from file "
              << FLAGS_private_key_pem_file;
  }
  return std::unique_ptr<AnalyzerServiceImpl>(new AnalyzerServiceImpl(
      observation_store, FLAGS_port, server_credentials, private_key_pem));
}

AnalyzerServiceImpl::AnalyzerServiceImpl(
    std::shared_ptr<store::ObservationStore> observation_store, int port,
    std::shared_ptr<grpc::ServerCredentials> server_credentials,
    const std::string& private_key_pem)
    : observation_store_(observation_store),
      port_(port),
      server_credentials_(server_credentials),
      message_decrypter_(private_key_pem) {}

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
  VLOG(3) << "Received batch of " << batch->encrypted_observation_size()
          << " observations.";
  for (const EncryptedMessage& em : batch->encrypted_observation()) {
    Observation observation;
    if (!message_decrypter_.DecryptMessage(em, &observation)) {
      std::string error_message = "Decryption of an Observation failed.";
      LOG(ERROR) << error_message;
      return grpc::Status(grpc::INVALID_ARGUMENT, error_message);
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

}  // namespace analyzer
}  // namespace cobalt
