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

#include "analyzer/store/bigtable_flags.h"

namespace cobalt {
namespace analyzer {
namespace store {

DEFINE_string(bigtable_project_name, "",
              "The name of Cobalt's Google Cloud project.");
DEFINE_string(bigtable_instance_id, "",
              "The name of Cobalt's Google Cloud Bigtable instance.");

DEFINE_bool(for_testing_only_use_bigtable_emulator, false,
            "If --for_cobalt_testing_only_use_memstore=false and this flag "
            "is true then use insecure client credentials to connect to "
            "the Bigtable Emulator running at the default port on localhost.");

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
