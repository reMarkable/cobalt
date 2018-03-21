// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CONFIG_VALIDATION_VALID_COBALT_CONFIG_H_
#define COBALT_CONFIG_VALIDATION_VALID_COBALT_CONFIG_H_

#include <memory>

#include "config/cobalt_config.pb.h"
#include "third_party/tensorflow_statusor/statusor.h"

namespace cobalt {
namespace config {
namespace validation {

using tensorflow_statusor::StatusOr;

// This represents a validated CobaltConfig object. If the StatusOr returned
// from GetValidCobaltConfig is a ValidCobaltConfig then the provided
// CobaltConfig is guaranteed to be valid.
class ValidCobaltConfig {
 public:
  // GetValidCobaltConfig attempts to construct a ValidCobaltConfig object using
  // the supplied CobaltConfig (|cfg|). If it runs into any validation errors,
  // it returns a util::Status with the validation error, otherwise it returns
  // the ValidCobaltConfig object.
  static StatusOr<ValidCobaltConfig> GetValidCobaltConfig(
      std::unique_ptr<CobaltConfig> cfg);

  const std::unique_ptr<CobaltConfig> &config() const { return config_; }

 private:
  explicit ValidCobaltConfig(std::unique_ptr<CobaltConfig> cfg);

  std::unique_ptr<CobaltConfig> config_;
};

}  // namespace validation
}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_VALIDATION_VALID_COBALT_CONFIG_H_
