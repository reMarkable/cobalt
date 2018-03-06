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
#include "util/encrypted_message_util.h"

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
// and shuffler as well as an encryption scheme.
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
  // Constructor
  //
  // |analyzer_public_key_pem| is a PEM encoding of the public key of the
  // Analyzer used for encrypting Observations that will be sent to the
  // Analyzer (by way of the Shuffler). It must be appropriate for the
  // encryption scheme specified by |analyzer_scheme|.
  //
  // |analyzer_scheme| The public key encryption scheme to use when encrypting
  // Observations sent to the Analyzer (by way of the Shuffler).
  //
  // |shuffler_public_key_pem| is a PEM encoding of the public key of the
  // Shuffler used for encrypting Envelopes that will be sent to the
  // Shuffler. It must be appropriate for the encryption scheme specified by
  // |shuffler_scheme|.
  //
  // |shuffler_scheme| The public key encryption scheme to use when encrypting
  // Envelopes sent to the Shuffler.
  //
  // |max_bytes_each_observation|. If specified then AddObservation() will
  // return kObservationTooBig if the provided observation's serialized,
  // encrypted size is greater than this value.
  //
  // |max_num_bytes|. If specified then AddObservation() will return
  // kEnvelopeFull if the provided observation's serialized, encrypted size
  // is not too large by itself, but adding the additional observation would
  // cause the sum of the sizes of all added Observations to be greater than
  // this value.
  EnvelopeMaker(const std::string& analyzer_public_key_pem,
                EncryptedMessage::EncryptionScheme analyzer_scheme,
                const std::string& shuffler_public_key_pem,
                EncryptedMessage::EncryptionScheme shuffler_scheme,
                size_t max_bytes_each_observation = SIZE_MAX,
                size_t max_num_bytes = SIZE_MAX);

  // The status of an AddObservation() call.
  enum AddStatus {
    // AddObservation() succeeded.
    kOk = 0,

    // The Observation was not added to the Envelope because it is
    // too big.
    kObservationTooBig,

    // The Observation was not added to the Envelope because the
    // envelope is full. The Observation itself is not too big
    // to be added otherwise.
    kEnvelopeFull,

    // The Observation was not added to the Envelope because the encryption
    // failed.
    kEncryptionFailed
  };

  AddStatus AddObservation(const Observation& observation,
                           std::unique_ptr<ObservationMetadata> metadata);

  // Populates |*encrypted_message| with the encryption of the current
  // value of the Envelope. Returns true for success or false for failure.
  bool MakeEncryptedEnvelope(EncryptedMessage* encrypted_message) const;

  // Gives direct read-only access to the internal instance of Envelope.
  const Envelope& envelope() const { return envelope_; }

  bool Empty() const { return envelope_.batch_size() == 0; }

  void Clear() {
    SystemProfile* saved_profile = envelope_.release_system_profile();
    envelope_.Clear();
    envelope_.set_allocated_system_profile(saved_profile);
    batch_map_.clear();
    num_bytes_ = 0;
  }

  // Moves the contents out of |*other| and merges it into |*this|.
  // Leaves |*other| empty.
  void MergeOutOf(EnvelopeMaker* other);

  // Returns an approximation to the size of the Envelope in bytes. This value
  // is the sum of the sizes of the serialized, encrypted Observations contained
  // in the Envelope. But the size of the EncryptedMessage produced by the
  // method MakeEncryptedEnvelope() may be somewhat larger than this because
  // the Envelope itself may be encrypted to the Shuffler.
  size_t size() { return num_bytes_; }

 private:
  friend class EnvelopeMakerTest;

  // Returns the ObservationBatch containing the given |metadata|. If
  // this is the first time we have seen the given |metadata| then a
  // new ObservationBatch is created.
  ObservationBatch* GetBatch(std::unique_ptr<ObservationMetadata> metadata);

  Envelope envelope_;
  util::EncryptedMessageMaker encrypt_to_analyzer_;
  util::EncryptedMessageMaker encrypt_to_shuffler_;

  // The keys of the map are serialized ObservationMetadata. The values
  // are the ObservationBatch containing that Metadata
  std::unordered_map<std::string, ObservationBatch*> batch_map_;

  // Keeps a running total of the sum of the sizes of the encrypted Observations
  // contained in |envelope_|;
  size_t num_bytes_ = 0;

  const size_t max_bytes_each_observation_;
  const size_t max_num_bytes_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_ENVELOPE_MAKER_H_
