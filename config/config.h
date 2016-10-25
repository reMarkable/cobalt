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

#ifndef COBALT_CONFIG_CONFIG_H_
#define COBALT_CONFIG_CONFIG_H_

#include <google/protobuf/io/tokenizer.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "./encodings.pb.h"

namespace cobalt {
namespace config {

enum Status {
  kOK = 0,

  // The specified file could not be opened.
  kFileOpenError = 1,

  // The specified file could not be parsed as the appropriate type
  // of protocol message.
  kParsingError = 2,

  // The specified file could be parsed but it contained two different
  // objects with the same fully-qualified ID.
  kDuplicateRegistration = 3
};

// A container for all of the |EncodingConfig|s registered in Cobalt. This
// is used both in the Encoder client and in the Analyzer.
class EncodingRegistry {
 public:
  // Populates a new instance of EncodingRegistry by reading and parsing the
  // specified file. Returns a pair consisting of a pointer to the result and a
  // Status.
  //
  // If the operation is successful then the status is kOK. Otherwise the
  // Status indicates the error.
  //
  // If |error_collector| is not null then it will be notified of any parsing
  // errors or warnings.
  static std::pair<std::unique_ptr<EncodingRegistry>, Status>
      FromFile(const std::string& file_path,
               google::protobuf::io::ErrorCollector* error_collector);

  // Returns the number of EncodingConfigs in this registry.
  size_t size();

  // Returns the EncodingConfig with the given ID triple, or nullptr if there is
  // no such Encodingconfig. The caller does not take ownership of the returned
  // pointer.
  const EncodingConfig* const Get(uint32_t customer_id,
                                  uint32_t project_id,
                                  uint32_t id);

 private:
  // The keys in this map are strings that encode ID triples of the form
  // (customer_id, project_id, id)
  std::unordered_map<std::string, std::unique_ptr<EncodingConfig>> map_;
};

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_CONFIG_H_
