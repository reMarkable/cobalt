// Copyright 2017 The Fuchsia Authors
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

#ifndef COBALT_ENCODER_SHUFFLER_CLIENT_H_
#define COBALT_ENCODER_SHUFFLER_CLIENT_H_

#include <memory>
#include <string>

#include "grpc++/grpc++.h"
#include "shuffler/shuffler.grpc.pb.h"

namespace cobalt {
namespace encoder {

class ShufflerClientInterface {
 public:
  virtual ~ShufflerClientInterface() = default;

  // Send the given |encrypted_message| to the Shuffler. It should be an
  // encryped Envelope as given by the output of
  // EnvelopeMaker::MakeEncryptedEnvelope().
  //
  // context. An optional grpc::Client context may be passed in allowing the
  // caller more control over the gRPC call. The context may be used for example
  // to set the deadline or to cancel the call.
  virtual grpc::Status SendToShuffler(
      const EncryptedMessage& encrypted_message,
      grpc::ClientContext* context = nullptr) = 0;
};

// This class provides a thin wrapper around the gRPC client to the Shuffler,
// allowing an Encoder client to optionally not deal with the details of gRPC.
class ShufflerClient : public ShufflerClientInterface {
 public:
  // Constructor.
  //
  // uri. The URI of the Shuffler service.
  //
  // use_tls. Should TLS be used to connect to the Shuffler?
  //
  // pem_root_certs. This value is ignored unless |use_tls| is true.
  // An optional override for the root certificates. If non NULL
  // this must point to a buffer containing a null-terminated PEM encoding of
  // the root CA certificates to use in TLS. If empty then a default will be
  // used. The default roots can also be overridden using the
  // GRPC_DEFAULT_SSL_ROOTS_FILE_PATH environment variable
  // pointing to a file on the file system containing the roots.
  ShufflerClient(const std::string& uri, bool use_tls,
                 const char* pem_root_certs = nullptr);

  virtual ~ShufflerClient() = default;

  // Send the given |encrypted_message| to the Shuffler. It should be an
  // encryped Envelope as given by the output of
  // EnvelopeMaker::MakeEncryptedEnvelope().
  //
  // context. An optional grpc::Client context may be passed in allowing the
  // caller more control over the gRPC call. The context may be used for example
  // to set the deadline or to cancel the call.
  grpc::Status SendToShuffler(const EncryptedMessage& encrypted_message,
                              grpc::ClientContext* context = nullptr) override;

 private:
  std::unique_ptr<shuffler::Shuffler::Stub> shuffler_stub_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_SHUFFLER_CLIENT_H_
