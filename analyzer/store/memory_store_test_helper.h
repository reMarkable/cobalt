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

#ifndef COBALT_ANALYZER_STORE_MEMORY_STORE_TEST_HELPER_H_
#define COBALT_ANALYZER_STORE_MEMORY_STORE_TEST_HELPER_H_

#include "analyzer/store/memory_store.h"

namespace cobalt {
namespace analyzer {
namespace store {

// MemoryStoreFactory is an example of a class that may be substituted
// for the StoreFactoryClass template parameter in the DataStoreTest
// template in data_store_test.h and in the ObservationStoreAbstractTest
// template in observation_store_abstract_test.h
class MemoryStoreFactory {
 public:
  static MemoryStore* NewStore() { return new MemoryStore(); }
};

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_MEMORY_STORE_TEST_HELPER_H_
