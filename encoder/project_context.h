// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_PROJECT_CONTEXT_H_
#define COBALT_ENCODER_PROJECT_CONTEXT_H_

#include <memory>
#include <utility>

#include "config/client_config.h"
#include "config/encoding_config.h"
#include "config/metric_config.h"

namespace cobalt {
namespace encoder {

// A ProjectContext represents a particular Cobalt project and contains
// a registry of the Metrics and EncodingConfigs contained in the project.
class ProjectContext {
 public:
  // Constructs a ProjectContext for the project with the given IDs
  // and containing the given metric and endoding registries.
  // DEPRECATED. Use the constructor that takes a ClientConfig instead.
  ProjectContext(uint32_t customer_id, uint32_t project_id,
                 std::shared_ptr<config::MetricRegistry> metric_registry,
                 std::shared_ptr<config::EncodingRegistry> encoding_registry);

  // Constructs a ProjectContext for the project with the given IDs
  // and ClientConfig.
  ProjectContext(uint32_t customer_id, uint32_t project_id,
                 std::shared_ptr<config::ClientConfig> client_config);

  // Returns the Metric with the given ID in the project, or nullptr if there is
  // no such Metric. The caller does not take ownership of the returned
  // pointer.
  const Metric* Metric(uint32_t id) const;

  // Returns the EncodingConfig with the given ID in the project, or nullptr if
  // there is no such EncodingConfig. The caller does not take ownership of the
  // returned pointer.
  const EncodingConfig* EncodingConfig(uint32_t id) const;

  uint32_t customer_id() const { return customer_id_; }

  uint32_t project_id() const { return project_id_; }

 private:
  const uint32_t customer_id_, project_id_;

  // Either client_config_ will be null or else
  // metric_registry_ and encoding_registry_ will be null, depending on
  // which constructor was used.
  std::shared_ptr<config::ClientConfig> client_config_;
  std::shared_ptr<config::MetricRegistry> metric_registry_;
  std::shared_ptr<config::EncodingRegistry> encoding_registry_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_PROJECT_CONTEXT_H_
