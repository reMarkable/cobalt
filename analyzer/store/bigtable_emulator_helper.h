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

#ifndef COBALT_ANALYZER_STORE_BIGTABLE_EMULATOR_HELPER_H_
#define COBALT_ANALYZER_STORE_BIGTABLE_EMULATOR_HELPER_H_

#include <glog/logging.h>

#include <chrono>

#include "analyzer/store/bigtable_admin.h"
#include "analyzer/store/bigtable_names.h"
#include "analyzer/store/bigtable_store.h"

// This file contains utilities useful to our Bigtable Emulator tests. These
// are tests that assume the existence of a local Bigtable Emulator process
// and connect to it via gRPC.

namespace cobalt {
namespace analyzer {
namespace store {

// A concrete instantiation of the parameter StoreFactoryClass used in several
// of our templated tests. The NewStore() function returns a BigtableStore*
// that will connect to the local Bigtable Emulator listening on the
// default port. NewStore() also ensures that the Bigtable Emulator is
// up and listening and that the Cobalt tables have been created.
class BigtableStoreEmulatorFactory {
 public:
  static BigtableStore* NewStore() {
    static const char kTestProject[] = "TestProject";
    static const char kTestInstance[] = "TestInstance";
    static const char kDefaultUrl[] = "localhost:9000";

    std::unique_ptr<BigtableAdmin> bigtable_admin;
    // Try three times to connect to the Bigtable Emulator.
    for (int attempt = 0; attempt < 3; attempt++) {
      bigtable_admin.reset(new BigtableAdmin(kDefaultUrl,
                                             grpc::InsecureChannelCredentials(),
                                             kTestProject, kTestInstance));
      // Wait for up to 5 seconds for the Bigtable Emulator to start listening.
      if (!bigtable_admin->WaitForConnected(std::chrono::system_clock::now() +
                                            std::chrono::seconds(5))) {
        bigtable_admin.reset();
      } else {
        break;
      }
    }
    if (!bigtable_admin) {
      LOG(FATAL) << "Unable to connect to Bigtable Emulator.";
    }

    // Make sure the tables have been created.
    if (!bigtable_admin->CreateTablesIfNecessary()) {
      LOG(FATAL) << "Unable to create the Cobalt BigTables.";
    }

    return new BigtableStore(kDefaultUrl, kDefaultUrl,
                             grpc::InsecureChannelCredentials(), kTestProject,
                             kTestInstance);
  }
};

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_BIGTABLE_EMULATOR_HELPER_H_
