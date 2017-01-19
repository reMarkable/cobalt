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

#include "encoder/envelope_maker.h"

#include <glog/logging.h>

#include <utility>

namespace cobalt {
namespace encoder {

EnvelopeMaker::EnvelopeMaker(std::string analyzer_public_key,
                             std::string shuffler_public_key)
    : analyzer_public_key_(std::move(analyzer_public_key)),
      shuffler_public_key_(std::move(shuffler_public_key)) {}

void EnvelopeMaker::AddObservation(
    const Observation& observation,
    std::unique_ptr<ObservationMetadata> metadata) {
  // Serialize the observation.
  std::string serialized_observation;
  observation.SerializeToString(&serialized_observation);

  // Encrypt the observation.
  // TODO(rudominer) Perform the encryption here. Encrypt using the Analyzer's
  // public key.
  std::string ciphertext = serialized_observation;

  // Put the encrypted observation into the appropriate ObservationBatch.
  EncryptedMessage* encrypted_message =
      GetBatch(std::move(metadata))->add_encrypted_observation();
  encrypted_message->mutable_ciphertext()->swap(ciphertext);
}

ObservationBatch* EnvelopeMaker::GetBatch(
    std::unique_ptr<ObservationMetadata> metadata) {
  // Serialize metadata.
  std::string serialized_metadata;
  (*metadata).SerializeToString(&serialized_metadata);

  // See if metadata is already in batch_map_. If so return it.
  auto iter = batch_map_.find(serialized_metadata);
  if (iter != batch_map_.end()) {
    return iter->second;
  }

  // Create a new ObservationBatch and add it to both envelope_ and batch_map_.
  ObservationBatch* observation_batch = envelope_.add_batch();
  observation_batch->set_allocated_meta_data(metadata.release());
  batch_map_[serialized_metadata] = observation_batch;
  return observation_batch;
}

EncryptedMessage EnvelopeMaker::MakeEncryptedEnvelope() {
  std::string serialized_envelope;
  envelope_.SerializeToString(&serialized_envelope);

  // TODO(rudominer) Perform the encryption here. Encrypt using the Shuffler's
  // public key.
  std::string ciphertext = serialized_envelope;

  EncryptedMessage encrypted_message;
  encrypted_message.mutable_ciphertext()->swap(ciphertext);
  return encrypted_message;
}

}  // namespace encoder
}  // namespace cobalt
