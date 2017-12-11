// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config/analyzer_config_manager.h"

#include <fstream>
#include <memory>

#include "config/analyzer_config.h"
#include "config/cobalt_config.pb.h"
#include "gflags/gflags.h"
#include "glog/logging.h"

namespace cobalt {
namespace config {

DEFINE_string(cobalt_config_proto_path, "",
              "Location on disk of the serialized CobaltConfig proto from "
              "which the Report Master's configuration is to be read.");

AnalyzerConfigManager::AnalyzerConfigManager(
    std::shared_ptr<AnalyzerConfig> config) {
  ptr_ = config;
}

std::shared_ptr<AnalyzerConfig> AnalyzerConfigManager::GetCurrent() {
  return ptr_;
}

std::shared_ptr<AnalyzerConfigManager>
AnalyzerConfigManager::CreateFromFlagsOrDie() {
  if (FLAGS_cobalt_config_proto_path.empty()) {
    auto config = AnalyzerConfig::CreateFromFlagsOrDie();
    return std::shared_ptr<AnalyzerConfigManager>(
        new AnalyzerConfigManager(std::move(config)));
  }

  // If a file containing a serialized CobaltConfig is specified, we load the
  // initial configuration from that file.
  std::ifstream config_file_stream;
  config_file_stream.open(FLAGS_cobalt_config_proto_path);
  if (!config_file_stream) {
    LOG(FATAL) << "Could not open initial config proto: "
               << FLAGS_cobalt_config_proto_path;
  }

  CobaltConfig cobalt_config;
  if (!cobalt_config.ParseFromIstream(&config_file_stream)) {
    LOG(FATAL) << "Could not parse the initial config proto: "
               << FLAGS_cobalt_config_proto_path;
  }

  auto config = AnalyzerConfig::CreateFromCobaltConfigProto(cobalt_config);
  if (!config) {
    LOG(FATAL) << "Error creating the initial AnalyzerConfig: "
               << FLAGS_cobalt_config_proto_path;
  }
  return std::shared_ptr<AnalyzerConfigManager>(
      new AnalyzerConfigManager(std::move(config)));
}

}  // namespace config
}  // namespace cobalt
