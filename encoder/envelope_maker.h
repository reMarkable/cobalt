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

#ifndef COBALT_ENCODER_ENVELOPE_MAKER_H_
#define COBALT_ENCODER_ENVELOPE_MAKER_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "./encrypted_message.pb.h"
#include "./observation.pb.h"

namespace cobalt {
namespace encoder {

// An Encoder client uses an EnvelopeMaker in conjunction with an Encoder in
// order to build the encrypted Envelopes that are sent to the shuffler.
// An EnvelopeMaker collects the Observations produced by the Encoder Encode*()
// methods into an Envelope. The MakeEncryptedEnvelope() returns an
// EncryptedMessage that contains the encryption of the Envelope.
//
// Usage:
//
// - Construct a new EnvelopeMaker passing in the public keys of the analyzer
// and shuffler.
//
// - Invoke AddObservation() multiple times passing in Observations and their
// corresponding ObservationMetadata. These are obtained from an Encoder.
//
// - When enough Observations have been added to send to the shuffler invoke
//   MakeEncryptedEnvelope(). The returned EncryptedMessage may be sent to
//   the shuffler.
//
// - Call Clear() to remove the Observations from the EnvelopeMaker. Then
//   the EnvelopeMaker can be used again.
class EnvelopeMaker {
 public:
  EnvelopeMaker(std::string analyzer_public_key,
                std::string shuffler_public_key);

  void AddObservation(const Observation& observation,
                      std::unique_ptr<ObservationMetadata> metadata);

  EncryptedMessage MakeEncryptedEnvelope();

  // Gives direct read-only access to the internal instance of Envelope.
  const Envelope& envelope() const { return envelope_; }

  void Clear() {
    envelope_ = Envelope();
    batch_map_.clear();
  }

 private:
  friend class EnvelopeMakerTest;

  // Returns the ObservationBatch containing the given |metadata|. If
  // this is the first time we have seen the given |metadata| then a
  // new ObservationBatch is created.
  ObservationBatch* GetBatch(std::unique_ptr<ObservationMetadata> metadata);

  Envelope envelope_;
  std::string analyzer_public_key_;
  std::string shuffler_public_key_;

  // The keys of the map are serialized ObservationMetadata. The values
  // are the ObservationBatch containing that Metadata
  std::unordered_map<std::string, ObservationBatch*> batch_map_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_ENVELOPE_MAKER_H_
