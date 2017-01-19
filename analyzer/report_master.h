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

#ifndef COBALT_ANALYZER_REPORT_MASTER_H_
#define COBALT_ANALYZER_REPORT_MASTER_H_

#include <atomic>

namespace cobalt {
namespace analyzer {

// This function is the main function for the ReportMaster's thread. In the
// future the ReportMaster will be a separate process and this will be
// replaced by the process's main. This call blocks until |shut_down| is set
// to true.
void ReportMasterMain(std::atomic<bool>* shut_down);

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_H_
