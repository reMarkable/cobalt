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

#include "util/pem_util.h"

#include <fstream>
#include <streambuf>
#include <string>

#include "glog/logging.h"

namespace cobalt {
namespace util {

bool PemUtil::ReadTextFile(const std::string& file_path,
                           std::string* file_contents) {
  if (!file_contents) {
    return false;
  }
  if (file_path.empty()) {
    return false;
  }
  std::ifstream stream(file_path, std::ifstream::in);
  if (!stream.good()) {
    // NOTE(rudominer) We use VLOG instead of LOG(ERROR) for any error messages
    // in Cobalt that may occur on the client side.
    VLOG(1) << "Unable to open file at " << file_path;
    return false;
  }
  stream.seekg(0, std::ios::end);
  if (!stream.good()) {
    VLOG(1) << "Error reading PEM file at " << file_path;
    return false;
  }
  // Don't try to read a file that's too big.
  auto file_size = stream.tellg();
  if (!stream.good() || file_size <= 0 || file_size > kMaxFileSize) {
    VLOG(1) << "Invalid file length for " << file_path;
    return false;
  }
  file_contents->reserve(file_size);

  stream.seekg(0, std::ios::beg);
  if (!stream.good()) {
    VLOG(1) << "Error reading file at " << file_path;
    return false;
  }
  file_contents->assign((std::istreambuf_iterator<char>(stream)),
                        std::istreambuf_iterator<char>());
  if (!stream.good()) {
    VLOG(1) << "Error reading file at " << file_path;
    return false;
  }
  VLOG(3) << "Successfully read file at " << file_path;
  return true;
}

}  // namespace util
}  // namespace cobalt
