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

#include "config/config.h"

#include <fcntl.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>
#include <string>

#include "config/config.pb.h"

namespace cobalt {
namespace config {

using google::protobuf::io::FileInputStream;
using google::protobuf::TextFormat;

namespace {

// Builds a map key that encodes the triple (customer_id, project_id, id)
const std::string MakeKey(uint32_t customer_id, uint32_t project_id,
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

const std::string MakeKey(const EncodingConfig& encoding_config) {
  return MakeKey(encoding_config.customer_id(), encoding_config.project_id(),
      encoding_config.id());
}

}  // namespace

std::pair<std::unique_ptr<EncodingRegistry>, Status>
    EncodingRegistry::FromFile(const std::string& file_path,
        google::protobuf::io::ErrorCollector* error_collector) {
  // Make an empty registry to return;
  auto registry = std::unique_ptr<EncodingRegistry>(new EncodingRegistry());

  // Try to open the specified file.
  int fd = open(file_path.c_str(), O_RDONLY);
  if (fd < 0) {
    return std::make_pair(std::move(registry), kFileOpenError);
  }

  // Try to parse the specified file.
  FileInputStream file_input_stream(fd);
  file_input_stream.SetCloseOnDelete(true);
  // The contents of the file should be a serialized |RegisteredEncodings|.
  RegisteredEncodings registered_encodings;
  TextFormat::Parser parser;
  if (error_collector) {
    parser.RecordErrorsTo(error_collector);
  }
  if (!parser.Parse(&file_input_stream, &registered_encodings)) {
    return std::make_pair(std::move(registry), kParsingError);
  }

  // Put all of the EncodingConfigs into the map, ensuring that the id triples
  // are unique.
  int num_encodings = registered_encodings.encoding_size();
  for (int i = 0; i < num_encodings; i++) {
    EncodingConfig* encoding_config = registered_encodings.mutable_encoding(i);
    // First build the key and insert an empty EncodingConfig into the map
    // at that key.
    auto pair = registry->map_.insert(std::make_pair(MakeKey(*encoding_config),
        std::unique_ptr<EncodingConfig>(new EncodingConfig())));
    const bool& success = pair.second;
    auto& inserted_pair = pair.first;
    if (!success) {
      return std::make_pair(std::move(registry), kDuplicateRegistration);
    }
    // Then swap in the data from the EncodingConfig;
    inserted_pair->second->Swap(encoding_config);
  }
  return std::make_pair(std::move(registry), kOK);
}

size_t EncodingRegistry::size() {
  return map_.size();
}

const EncodingConfig* const EncodingRegistry::Get(uint32_t customer_id,
    uint32_t project_id, uint32_t id) {
  auto iterator = map_.find(MakeKey(customer_id, project_id, id));
  if (iterator == map_.end()) {
    return nullptr;
  }
  return iterator->second.get();
}



}  // namespace config
}  // namespace cobalt

