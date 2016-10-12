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

#include "analyzer/store/mem_store.h"

namespace cobalt {
namespace analyzer {

int MemStore::put(const std::string& key, const std::string& val) {
  data_[key] = val;

  return 0;
}

int MemStore::get(const std::string& key, std::string* out) {
  auto i = data_.find(key);

  if (i == data_.end())
    return -1;

  *out = i->second;

  return 0;
}

}  // namespace analyzer
}  // namespace cobalt
