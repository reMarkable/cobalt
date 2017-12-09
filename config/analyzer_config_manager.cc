// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config/analyzer_config_manager.h"

#include <memory>

#include "config/analyzer_config.h"

namespace cobalt {
namespace config {

AnalyzerConfigManager::AnalyzerConfigManager(
    std::shared_ptr<AnalyzerConfig> config) {
  ptr_ = config;
}

std::shared_ptr<AnalyzerConfig> AnalyzerConfigManager::GetCurrent() {
  return ptr_;
}

std::shared_ptr<AnalyzerConfigManager>
AnalyzerConfigManager::CreateFromFlagsOrDie() {
  auto config = AnalyzerConfig::CreateFromFlagsOrDie();
  return std::shared_ptr<AnalyzerConfigManager>(
      new AnalyzerConfigManager(std::move(config)));
}

}  // namespace config
}  // namespace cobalt
