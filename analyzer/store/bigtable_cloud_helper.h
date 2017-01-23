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

#ifndef COBALT_ANALYZER_STORE_BIGTABLE_CLOUD_HELPER_H_
#define COBALT_ANALYZER_STORE_BIGTABLE_CLOUD_HELPER_H_

#include "analyzer/store/bigtable_admin.h"
#include "analyzer/store/bigtable_names.h"
#include "analyzer/store/bigtable_store.h"
#include "analyzer/store/data_store_test.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {
namespace store {

// A concrete instantiation of the parameter StoreFactoryClass used in several
// of our templated tests. The NewStore() function returns a BigtableStore*
// that will connect to the real Cloud Bigtable. NewStore() also ensures that
// Cobalt tables have been created.
//
// In order to connect successfully to the real Google Cloud Bigtable several
// items must be set up in the environment in which the tests that use this
// factory are run.
class BigtableStoreCloudFactory {
 public:
  static BigtableStore* NewStore() {
    auto admin = BigtableAdmin::CreateFromFlagsOrDie();

    // The Cloud Bigtable instance we are accessing may have started up
    // recently wait for up to 10 seconds for it to start listening.
    if (!admin->WaitForConnected(std::chrono::system_clock::now() +
                                 std::chrono::seconds(10))) {
      LOG(FATAL) << "Waited for 10 seconds to connect to Cloud Bigtable.";
    }

    // Make sure the tables have been created.
    if (!admin->CreateTablesIfNecessary()) {
      LOG(FATAL) << "Unable to create the Cobalt BigTables.";
    }

    return BigtableStore::CreateFromFlagsOrDie().release();
  }
};

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_BIGTABLE_CLOUD_HELPER_H_
