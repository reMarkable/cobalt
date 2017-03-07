// Copyright 2017 The Fuchsia Authors
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

#import "tools/test_app/test_app.h"

#include "gflags/gflags.h"
#include "glog/logging.h"

int main(int argc, char* argv[]) {
  google::SetUsageMessage(
      "Cobalt test client application.\n"
      "There are three modes of operation controlled by the -mode flag:\n"
      "interactive: The program runs an interactive command-loop.\n"
      "send-once: The program sends a single Envelope described by the flags.\n"
      "automatic: The program runs forever sending many Envelopes with "
      "randomly generated values.");
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  auto app = cobalt::TestApp::CreateFromFlagsOrDie(argc, argv);
  app->Run();

  exit(0);
}
