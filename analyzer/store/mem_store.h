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

#ifndef COBALT_ANALYZER_STORE_MEM_STORE_H_
#define COBALT_ANALYZER_STORE_MEM_STORE_H_

#include <map>
#include <string>

#include "analyzer/store/store.h"

namespace cobalt {
namespace analyzer {

// A singleton in-memory key value store using std::map.
class MemStoreSingleton {
 public:
  static MemStoreSingleton& instance();
  int put(const std::string& key, const std::string& val);
  int get(const std::string& key, std::string* out);

 private:
  MemStoreSingleton() {}

  // Used for debugging.  Will return a formatted version of the key and value.
  std::string to_string(const std::string& key, const std::string& val);

  std::map<std::string, std::string> data_;

  friend class AnalyzerFunctionalTest;
};

// An in-memory store.  The backing store is a singleton shared by all MemStore
// instances.
class MemStore : public Store {
 public:
  int put(const std::string& key, const std::string& val) override {
    return MemStoreSingleton::instance().put(key, val);
  }

  int get(const std::string& key, std::string* out) override {
    return MemStoreSingleton::instance().get(key, out);
  }

  int get_range(const std::string& start, const std::string& end,
                std::map<std::string, std::string>* out) override {
    return -1;  // TODO(bittau): implement this.
  }
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_MEM_STORE_H_
