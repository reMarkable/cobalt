// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "./logging.h"
#include "encoder/observation_store.h"

namespace cobalt {
namespace encoder {

ObservationStore::ObservationStore(size_t max_bytes_per_observation,
                                   size_t max_bytes_per_envelope,
                                   size_t max_bytes_total,
                                   size_t min_bytes_per_envelope)
    : max_bytes_per_observation_(max_bytes_per_observation),
      max_bytes_per_envelope_(max_bytes_per_envelope),
      max_bytes_total_(max_bytes_total),
      min_bytes_per_envelope_(min_bytes_per_envelope),
      almost_full_threshold_(0.6 * max_bytes_total_) {
  CHECK_LE(max_bytes_per_observation_, max_bytes_per_envelope_);
  CHECK_LE(max_bytes_per_envelope_, max_bytes_total_);
  CHECK_LE(min_bytes_per_envelope_, max_bytes_per_envelope_);
}

bool ObservationStore::IsAlmostFull() const {
  return Size() > almost_full_threshold_;
}

std::string ObservationStore::StatusDebugString(StoreStatus status) {
  switch (status) {
    case kOk:
      return "kOk";

    case kObservationTooBig:
      return "kObservationTooBig";

    case kStoreFull:
      return "kStoreFull";

    case kWriteFailed:
      return "kWriteFailed";
  }
}

}  // namespace encoder
}  // namespace cobalt
