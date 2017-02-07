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

#include "analyzer/report_master/report_generator_abstract_test.h"
#include "analyzer/store/bigtable_cloud_helper.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {
namespace store {

// We instantiate ReportGeneratorAbstractTest using an instance of
// BigtableStore connected to the real Cloud Bigtable as the underlying
// DataStore. See notes in bigtable_cloud_helper.h

INSTANTIATE_TYPED_TEST_CASE_P(ReportGeneratorCloudTest,
                              ReportGeneratorAbstractTest,
                              BigtableStoreCloudFactory);

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
