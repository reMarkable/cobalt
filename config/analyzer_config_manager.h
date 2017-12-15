// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CONFIG_ANALYZER_CONFIG_MANAGER_H_
#define COBALT_CONFIG_ANALYZER_CONFIG_MANAGER_H_

#include <memory>

#include "config/analyzer_config.h"

namespace cobalt {
namespace config {

// AnalyzerConfigManager vends shared pointers to an AnalyzerConfig.
// The purpose of this class is to be able to update the configuration data
// pointers to which it vends.
class AnalyzerConfigManager {
 public:
  // Get a pointer to the current analyzer config. Do not cache.
  std::shared_ptr<AnalyzerConfig> GetCurrent();

  static std::shared_ptr<AnalyzerConfigManager> CreateFromFlagsOrDie();

  explicit AnalyzerConfigManager(std::shared_ptr<AnalyzerConfig> config);

 private:
  std::shared_ptr<AnalyzerConfig> ptr_;
};

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_ANALYZER_CONFIG_MANAGER_H_
