// Copyright 2016 The Fuchsia Authors
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

#ifndef COBALT_ENCODER_PROJECT_CONTEXT_H_
#define COBALT_ENCODER_PROJECT_CONTEXT_H_

#include <memory>
#include <utility>

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
  ProjectContext(uint32_t customer_id, uint32_t project_id,
                 std::shared_ptr<config::MetricRegistry> metric_registry,
                 std::shared_ptr<config::EncodingRegistry> encoding_registry)
      : customer_id_(customer_id),
        project_id_(project_id),
        metric_registry_(metric_registry),
        encoding_registry_(encoding_registry) {}

  // Returns the Metric with the given ID in the project, or nullptr if there is
  // no such MetricConfig. The caller does not take ownership of the returned
  // pointer.
  const Metric* Metric(uint32_t id) const {
    return metric_registry_->Get(customer_id_, project_id_, id);
  }

  // Returns the EncodingConfig with the given ID in the project, or nullptr if
  // there is no such EncodingConfig. The caller does not take ownership of the
  // returned pointer.
  const EncodingConfig* EncodingConfig(uint32_t id) const {
    return encoding_registry_->Get(customer_id_, project_id_, id);
  }

  uint32_t customer_id() const { return customer_id_; }

  uint32_t project_id() const { return project_id_; }

 private:
  const uint32_t customer_id_, project_id_;
  std::shared_ptr<config::MetricRegistry> metric_registry_;
  std::shared_ptr<config::EncodingRegistry> encoding_registry_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_PROJECT_CONTEXT_H_
