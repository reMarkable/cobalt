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

#ifndef COBALT_ENCODER_ENCODER_H_
#define COBALT_ENCODER_ENCODER_H_

#include <memory>
#include <string>
#include <vector>

#include "./cobalt.pb.h"
#include "./encrypted_message.pb.h"
#include "config/config.h"
#include "encoder/client_secret.h"
#include "encoder/date_policy.h"
#include "encoder/shuffler.pb.h"

namespace cobalt {
namespace encoder {

// Static configuration data for an Encoder client. This is read out of
// a local configuration file on the client.
struct EncoderClientConfig {
  uint32_t customer_id;
  uint32_t project_id;
  std::string shuffler_location;
  std::string shuffler_public_key;
  std::string analyzer_location;
  std::string analyzer_public_key;
};

class Encoder {
 public:
  // Constructs an Encoder with the following data:
  //
  // client_config: Static config describing the Encoder client and the
  //                Shuffler and Analyzer.
  //
  // client_secret: A random secret that is generated once on the client
  //                and then persisted on the client.
  //
  // encoding_registry: Gives the set of all registered EncodingConfigs.
  //
  // metrics_registry: Gives the set of all registeredMetrics.
  Encoder(const EncoderClientConfig& client_config, ClientSecret client_secret,
          std::unique_ptr<config::EncodingRegistry> encoding_registry
          std::unique_ptr<config::MetricRegistry> metric_registry);

  // Contains a single part of a value to be encoded. A value may contain
  // more than one part in which case the resulting Observation will contain
  // more than one part. Also contains the ID of the EncodingConfig that
  // should be used to encode this part.
  struct ValuePart {
    std::string part_name;
    // The fully-qualified identifier for an EncodingConfig is the triple
    // (customer_id, user_id, encoding_config_id.) The customer_id and
    // user_id were specified in the |client_config| passed to the constructor.
    uint32_t encoding_config_id;

    // The un-encoded value to be encoded.
    std::string data;
  };

  // A value of a metric to be encoded into an Obsevation. This is the input
  // to Encode().
  struct Value {
    // The fully-qualified identifier for a Metric is the triple
    // (customer_id, user_id, metric_id.) The customer_id and
    // user_id were specified in the |client_config| passed to the constructor.
    uint32_t metric_id;
    std::vector<ValuePart> parts;
  };

  enum Status {
    kOK = 0,
    kInvalidEncodingConfig
  };

  // Encodes |value| into |encrypted_message|. Returns kOK on success or an
  // error Status on failure.
  //
  // The EncryptedMessage will contain the encryption of an Envelope to be
  // sent to the Shuffler. The Envelope will contain a Manifest and the Manifest
  // will specify the provided |policy|.
  Status Encode(const Value& value,
                const shuffler::Manifest_ShufflerPolicy& policy,
                EncryptedMessage* encrypted_message);
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_ENCODER_H_
