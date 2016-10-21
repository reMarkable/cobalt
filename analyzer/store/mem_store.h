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

// An in-memory key value store using std::map
class MemStore : public Store {
 public:
  int put(const std::string& key, const std::string& val) override;
  int get(const std::string& key, std::string* out) override;

 private:
  // Used for debugging.  Will return a formatted version of the key and value.
  std::string to_string(const std::string& key, const std::string& val);

 public:
  std::map<std::string, std::string> data_;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_MEM_STORE_H_
