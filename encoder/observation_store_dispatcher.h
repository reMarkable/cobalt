// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_OBSERVATION_STORE_DISPATCHER_H_
#define COBALT_ENCODER_OBSERVATION_STORE_DISPATCHER_H_

#include <map>
#include <memory>

#include "encoder/observation_store.h"
#include "third_party/tensorflow_statusor/statusor.h"

namespace cobalt {
namespace encoder {

// ObservationStoreDispatcher is a wrapper around implementations of the
// ObservationStore interface. It allows dispatching to multiple different
// ObservationStores so that we can send observations to different stores based
// on their destination backend. (Current backends are GKE GRPC, and a dummy
// Clearcut uploader). See ObservationStore for more details.
class ObservationStoreDispatcher {
 public:
  tensorflow_statusor::StatusOr<ObservationStore::StoreStatus>
  AddEncryptedObservation(std::unique_ptr<EncryptedMessage> message,
                          std::unique_ptr<ObservationMetadata> metadata);

  tensorflow_statusor::StatusOr<ObservationStore *> GetStore(
      ObservationMetadata::ShufflerBackend backend);

  void Register(ObservationMetadata::ShufflerBackend backend,
                std::unique_ptr<ObservationStore> store);

 private:
  std::map<ObservationMetadata::ShufflerBackend,
           std::unique_ptr<ObservationStore>>
      observation_stores_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_OBSERVATION_STORE_DISPATCHER_H_
