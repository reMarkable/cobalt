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

#include <glog/logging.h>

namespace cobalt {
namespace analyzer {

MemStoreSingleton& MemStoreSingleton::instance() {
  static MemStoreSingleton singleton;

  return singleton;
}

int MemStoreSingleton::put(const std::string& key, const std::string& val) {
  data_[key] = val;

  VLOG(1) << "put: " << to_string(key, val);

  return 0;
}

int MemStoreSingleton::get(const std::string& key, std::string* out) {
  auto i = data_.find(key);

  if (i == data_.end())
    return -1;

  *out = i->second;

  return 0;
}

std::string MemStoreSingleton::to_string(const std::string& key,
                                         const std::string& val) {
  std::ostringstream oss;

  oss << "Key [" << key << "]";
  oss << " Val sz " << val.size() << " [";

  for (int i = 0; i < val.size(); i++) {
    char tmp[3];

    snprintf(tmp, sizeof(tmp), "%.2x", val[i]);

    oss << tmp << " ";
  }

  oss << "]";

  return oss.str();
}

}  // namespace analyzer
}  // namespace cobalt
