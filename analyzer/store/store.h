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

#ifndef COBALT_ANALYZER_STORE_STORE_H_
#define COBALT_ANALYZER_STORE_STORE_H_

#include <map>
#include <memory>
#include <string>

namespace cobalt {
namespace analyzer {

// Interface to a key value store.
class Store {
 public:
  virtual ~Store() = 0;

  // Returns 0 on success.
  virtual int put(const std::string& key, const std::string& val) = 0;

  // Returns 0 on success.
  virtual int get(const std::string& key, std::string* out) = 0;

  // if start or end are the empty string, then the range is unbounded (e.g.,
  // starts at first element, or ends at the last element).
  // out is a map containing key -> value.
  //
  // TODO(bittau): implement a streaming interface or pagination as needed.
  // Right now the client has no way of controlling how many records are
  // returned.
  //
  // Returns 0 on success
  virtual int get_range(const std::string& start, const std::string& end,
                        std::map<std::string, std::string>* out)
                        = 0;
};

// This is a factory method that'll create a Store based on command line flags.
// By default a BigtableStore accessing the table name specified in the -table
// command line argument will be constructed.  If the -memstore is passed on the
// command line, a MemStore is created instead.
//
// If init_schema is true, necessary tables will be created, if needed.
std::unique_ptr<Store> MakeStore(bool init_schema);

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_STORE_STORE_H_
