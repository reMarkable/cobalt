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

#include <err.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <string>
#include <thread>

#include "analyzer/analyzer_service.h"
#include "analyzer/reporter.h"

int main(int argc, char *argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  // Right now we combine both the analyzer service and the reporter component
  // into one process, using two threads.  Each component has its own _main
  // method making it easy to speparate them into multiple programs in the
  // future.  Their _main methods would be folded into the top-level main().
  std::thread reporter(cobalt::analyzer::reporter_main);
  cobalt::analyzer::analyzer_service_main();
  reporter.join();

  return 0;
}
