// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "./logging.h"
#include "encoder/memory_observation_store.h"

namespace cobalt {
namespace encoder {

MemoryObservationStore::MemoryObservationStore(size_t max_bytes_per_observation,
                                               size_t max_bytes_per_envelope,
                                               size_t max_bytes_total,
                                               size_t min_bytes_per_envelope)
    : ObservationStore(max_bytes_per_observation, max_bytes_per_envelope,
                       max_bytes_total, min_bytes_per_envelope),
      envelope_send_threshold_size_(size_t(0.6 * max_bytes_per_envelope_)),
      current_envelope_(
          new EnvelopeMaker(max_bytes_per_observation, max_bytes_per_envelope)),
      finalized_envelopes_size_(0) {}

ObservationStore::StoreStatus MemoryObservationStore::AddEncryptedObservation(
    std::unique_ptr<EncryptedMessage> message,
    std::unique_ptr<ObservationMetadata> metadata) {
  std::unique_lock<std::mutex> lock(envelope_mutex_);

  if (SizeLocked() > max_bytes_total_) {
    VLOG(4) << "MemoryObservationStore::AddEncryptedObservation(): Rejecting "
               "observation because the store is full. ("
            << SizeLocked() << " > " << max_bytes_total_ << ")";
    return kStoreFull;
  }

  auto status = current_envelope_->CanAddObservation(*message);

  if (status == kStoreFull) {
    VLOG(4) << "MemoryObservationStore::AddEncryptedObservation(): Current "
               "envelope would return kStoreFull. Swapping it out for "
               "a new EnvelopeMaker";
    AddEnvelopeToSend(std::move(current_envelope_));
    current_envelope_ = NewEnvelopeMaker();
  }

  return current_envelope_->AddEncryptedObservation(std::move(message),
                                                    std::move(metadata));
}

std::unique_ptr<EnvelopeMaker> MemoryObservationStore::NewEnvelopeMaker() {
  return std::make_unique<EnvelopeMaker>(max_bytes_per_observation_,
                                         max_bytes_per_envelope_);
}

std::unique_ptr<ObservationStore::EnvelopeHolder>
MemoryObservationStore::TakeOldestEnvelopeHolderLocked() {
  auto retval = std::move(finalized_envelopes_.front());
  finalized_envelopes_.pop_front();
  if (retval->Size() > finalized_envelopes_size_) {
    finalized_envelopes_size_ = 0;
  } else {
    finalized_envelopes_size_ -= retval->Size();
  }
  return retval;
}

void MemoryObservationStore::AddEnvelopeToSend(
    std::unique_ptr<EnvelopeHolder> holder, bool back) {
  finalized_envelopes_size_ += holder->Size();
  if (back) {
    finalized_envelopes_.push_back(std::move(holder));
  } else {
    finalized_envelopes_.push_front(std::move(holder));
  }
}

std::unique_ptr<ObservationStore::EnvelopeHolder>
MemoryObservationStore::TakeNextEnvelopeHolder() {
  std::unique_lock<std::mutex> lock(envelope_mutex_);

  auto retval = NewEnvelopeMaker();
  size_t retval_size = 0;
  while (!finalized_envelopes_.empty() &&
         (retval_size == 0 ||
          (retval_size + finalized_envelopes_.front()->Size() <=
           max_bytes_per_envelope_))) {
    retval->MergeWith(TakeOldestEnvelopeHolderLocked());
    retval_size = retval->Size();
  }

  if (retval_size + current_envelope_->Size() <= max_bytes_per_envelope_) {
    retval->MergeWith(std::move(current_envelope_));
    current_envelope_ = NewEnvelopeMaker();
  }

  if (retval->Size() == 0) {
    return nullptr;
  }

  return retval;
}

void MemoryObservationStore::ReturnEnvelopeHolder(
    std::unique_ptr<ObservationStore::EnvelopeHolder> envelope) {
  std::unique_lock<std::mutex> lock(envelope_mutex_);
  AddEnvelopeToSend(std::move(envelope));
}

size_t MemoryObservationStore::SizeLocked() const {
  return current_envelope_->Size() + finalized_envelopes_size_;
}

size_t MemoryObservationStore::Size() const {
  std::unique_lock<std::mutex> lock(envelope_mutex_);
  return SizeLocked();
}

bool MemoryObservationStore::Empty() const {
  std::unique_lock<std::mutex> lock(envelope_mutex_);
  return current_envelope_->Empty() && finalized_envelopes_.empty();
}

}  // namespace encoder
}  // namespace cobalt
