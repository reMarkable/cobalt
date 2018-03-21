// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config/cobalt_config.pb.h"
#include "config/validation/valid_cobalt_config.h"

#include "gflags/gflags.h"
#include "glog/logging.h"

// This program reads a serialized CobaltConfig proto from stdin and checks for
// validation errors. If there are any, they will be printed to stdout.
int main(int argc, char *argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  std::cin >> std::noskipws;

  std::istream_iterator<char> it(std::cin);
  std::istream_iterator<char> end;
  std::string strProto(it, end);

  auto cfg = std::make_unique<cobalt::CobaltConfig>();
  cfg->ParseFromString(strProto);

  auto validCfg(
      cobalt::config::validation::ValidCobaltConfig::GetValidCobaltConfig(
          std::move(cfg)));
  if (!validCfg.ok()) {
    std::cout << validCfg.status().error_message();
  }
}
