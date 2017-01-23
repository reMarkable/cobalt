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

#include "analyzer/store/data_store.h"

#include "analyzer/store/bigtable_store.h"
#include "analyzer/store/memory_store.h"
#include "gflags/gflags.h"
#include "glog/logging.h"

namespace cobalt {
namespace analyzer {
namespace store {

// Flags used to put the DataStore into testing/debug mode
DEFINE_bool(for_testing_only_use_memstore, false,
            "Use MemoryStore as the underlying data store");

DataStore::~DataStore() {}

std::unique_ptr<DataStore> DataStore::CreateFromFlagsOrDie() {
  if (FLAGS_for_testing_only_use_memstore) {
    LOG(WARNING)
        << "**** Using an in-memory data store instead of BigTable. ****";
    return std::unique_ptr<DataStore>(new MemoryStore());
  }

  return BigtableStore::CreateFromFlagsOrDie();
}

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
