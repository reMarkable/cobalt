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

#include <fcntl.h>
#include <google/protobuf/io/tokenizer.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

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

// A template for a Registry.
//
// RT should be one of:
// RegisteredEncodings, RegisteredReports, RegisteredMetrics.
//
// We then define |T| to be the corresponding one of:
// EncodingConfig, ReportConfig, Metric.
//
// A Registry<RT> is then a container for all of the |T|s registered in Cobalt.
template<class RT> class Registry {
 public:
  // This is some template meta-programming magic that has the effect of
  // defining |T| to be the type of the objects contained in a registry
  // of type |RT|.
  typedef typename std::remove_pointer<decltype(
      (reinterpret_cast<RT*>(0))->mutable_element(0))>::type T;

  // Populates a new instance of Registry<T> by reading and parsing the
  // specified file. Returns a pair consisting of a pointer to the result and a
  // Status.
  //
  // If the operation is successful then the status is kOK. Otherwise the
  // Status indicates the error.
  //
  // If |error_collector| is not null then it will be notified of any parsing
  // errors or warnings.
  static std::pair<std::unique_ptr<Registry<RT>>, Status>
      FromFile(const std::string& file_path,
               google::protobuf::io::ErrorCollector* error_collector);

  // Returns the number of |T| in this registry.
  size_t size();

  // Returns the |T| with the given ID triple, or nullptr if there is
  // no such |T|. The caller does not take ownership of the returned
  // pointer.
  const T* const Get(uint32_t customer_id,
                     uint32_t project_id,
                     uint32_t id) {
    auto iterator = map_.find(MakeKey(customer_id, project_id, id));
    if (iterator == map_.end()) {
      return nullptr;
    }
    return iterator->second.get();
  }

 private:
  // Builds a map key that encodes the triple (customer_id, project_id, id)
  static std::string MakeKey(uint32_t customer_id, uint32_t project_id,
                             uint32_t id);

  // Builds a map key from the ids in |config_proto|.
  static std::string MakeKey(const T& config_proto);

  // The keys in this map are strings that encode ID triples of the form
  // (customer_id, project_id, id)
  std::unordered_map<std::string, std::unique_ptr<T>> map_;
};

//////////////////////////////////////////////////////////////////
/// IMPLEMENTATION BELOW
//////////////////////////////////////////////////////////////////

template<class RT>
std::string Registry<RT>::MakeKey(uint32_t customer_id, uint32_t project_id,
                                     uint32_t id) {
  // Three 32-bit positive ints (at most 10 digits each) plus 3 colons plus a
  // trailing null is <= 34 bytes.
  char out[34];
  int size = snprintf(out, sizeof(out), "%u:%u:%u",
                      customer_id, project_id, id);
  if (size <= 0) {
    return "";
  }
  return std::string(out, size);
}

template<class RT>
std::string Registry<RT>::MakeKey(const T& config_proto) {
  return MakeKey(config_proto.customer_id(), config_proto.project_id(),
                 config_proto.id());
}

template<class RT>
std::pair<std::unique_ptr<Registry<RT>>, Status>
    Registry<RT>::FromFile(const std::string& file_path,
        google::protobuf::io::ErrorCollector* error_collector) {
  // Make an empty registry to return;
  auto registry = std::unique_ptr<Registry<RT>>(new Registry<RT>());

  // Try to open the specified file.
  int fd = open(file_path.c_str(), O_RDONLY);
  if (fd < 0) {
    return std::make_pair(std::move(registry), kFileOpenError);
  }

  // Try to parse the specified file.
  google::protobuf::io::FileInputStream file_input_stream(fd);
  file_input_stream.SetCloseOnDelete(true);
  // The contents of the file should be a serialized |RT|.
  RT registered_configs;
  google::protobuf::TextFormat::Parser parser;
  if (error_collector) {
    parser.RecordErrorsTo(error_collector);
  }
  if (!parser.Parse(&file_input_stream, &registered_configs)) {
    return std::make_pair(std::move(registry), kParsingError);
  }

  // Put all of the T's into the map, ensuring that the id triples
  // are unique.
  int num_configs = registered_configs.element_size();
  for (int i = 0; i < num_configs; i++) {
    T* config_proto = registered_configs.mutable_element(i);
    // First build the key and insert an empty Tg into the map
    // at that key.
    auto pair = registry->map_.insert(std::make_pair(MakeKey(*config_proto),
        std::unique_ptr<T>(new T())));
    const bool& success = pair.second;
    auto& inserted_pair = pair.first;
    if (!success) {
      return std::make_pair(std::move(registry), kDuplicateRegistration);
    }
    // Then swap in the data from the T;
    inserted_pair->second->Swap(config_proto);
  }
  return std::make_pair(std::move(registry), kOK);
}

template<class RT>
size_t Registry<RT>::size() {
  return map_.size();
}

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_CONFIG_H_
