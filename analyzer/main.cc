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

#include <atomic>
#include <string>
#include <thread>

#include "analyzer/analyzer_service.h"
#include "analyzer/report_master.h"

int main(int argc, char *argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  // In Cobatl V0.1 the ReportMaster is run in another thread of this
  // process. In the future we expect it will be a seperate process.
  std::atomic<bool> shut_down_reporter(false);
  std::thread reporter(cobalt::analyzer::ReportMasterMain,
                       &shut_down_reporter);

  auto analyzer = cobalt::analyzer::AnalyzerServiceImpl::CreateFromFlagsOrDie();
  analyzer->Start();
  analyzer->Wait();

  shut_down_reporter = true;
  reporter.join();

  return 0;
}
