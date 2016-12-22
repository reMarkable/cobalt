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

namespace cobalt {
namespace analyzer {

// This is the main method for the report master.  This call blocks
// forever.  Currently it is not folded into main() because we run both the
// analyzer_service and the report master in a single process and each have
// their own "main()".
void report_master_main();

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_H_
