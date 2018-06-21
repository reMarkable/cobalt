// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "encoder/observation_store_dispatcher.h"
#include "third_party/tensorflow_statusor/status_macros.h"

namespace cobalt {
namespace encoder {

using tensorflow_statusor::StatusOr;
typedef ObservationMetadata::ShufflerBackend ShufflerBackend;

StatusOr<ObservationStore::StoreStatus>
ObservationStoreDispatcher::AddEncryptedObservation(
    std::unique_ptr<EncryptedMessage> message,
    std::unique_ptr<ObservationMetadata> metadata) {
  ObservationStore *s;
  CB_ASSIGN_OR_RETURN(s, GetStore(metadata->backend()));
  return s->AddEncryptedObservation(std::move(message), std::move(metadata));
}

void ObservationStoreDispatcher::Register(
    ShufflerBackend backend, std::unique_ptr<ObservationStore> store) {
  observation_stores_[backend] = std::move(store);
}

StatusOr<ObservationStore *> ObservationStoreDispatcher::GetStore(
    ShufflerBackend backend) {
  if (observation_stores_.find(backend) == observation_stores_.end()) {
    std::ostringstream ss;
    ss << "Could not find observation store for backend #" << backend;
    return util::Status(util::StatusCode::NOT_FOUND, ss.str());
  } else {
    return observation_stores_[backend].get();
  }
}

}  // namespace encoder
}  // namespace cobalt
