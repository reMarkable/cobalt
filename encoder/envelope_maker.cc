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

#include <utility>

#include "./logging.h"

namespace cobalt {
namespace encoder {

EnvelopeMaker::EnvelopeMaker(const std::string& analyzer_public_key_pem,
                             EncryptedMessage::EncryptionScheme analyzer_scheme,
                             const std::string& shuffler_public_key_pem,
                             EncryptedMessage::EncryptionScheme shuffler_scheme)
    : encrypt_to_analyzer_(analyzer_public_key_pem, analyzer_scheme),
      encrypt_to_shuffler_(shuffler_public_key_pem, shuffler_scheme) {}

void EnvelopeMaker::AddObservation(
    const Observation& observation,
    std::unique_ptr<ObservationMetadata> metadata) {
  EncryptedMessage encrypted_message;
  if (encrypt_to_analyzer_.Encrypt(observation, &encrypted_message)) {
    // Put the encrypted observation into the appropriate ObservationBatch.
    GetBatch(std::move(metadata))
        ->add_encrypted_observation()
        ->Swap(&encrypted_message);
  } else {
    VLOG(1)
        << "ERROR: Encryption of Observations failed! Observation not added "
           "to batch.";
  }
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

bool EnvelopeMaker::MakeEncryptedEnvelope(
    EncryptedMessage* encrypted_message) const {
  if (!encrypt_to_shuffler_.Encrypt(envelope_, encrypted_message)) {
    VLOG(1) << "ERROR: Encryption of Envelope to the Shuffler failed!";
    return false;
  }
  return true;
}

}  // namespace encoder
}  // namespace cobalt
