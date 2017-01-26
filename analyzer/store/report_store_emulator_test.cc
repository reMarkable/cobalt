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

#include "analyzer/store/bigtable_emulator_helper.h"
#include "analyzer/store/report_store_abstract_test.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {
namespace store {

// We instantiate ReportStoreAbstractTest using an instance of
// BigtableStore connected to a local Bigtable Emulator as the underlying
// DataStore. It is assumed that the Bigtable Emulator is running on localhost
// at the default port (9000).

INSTANTIATE_TYPED_TEST_CASE_P(ReportStoreEmulatorTest, ReportStoreAbstractTest,
                              BigtableStoreEmulatorFactory);

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
