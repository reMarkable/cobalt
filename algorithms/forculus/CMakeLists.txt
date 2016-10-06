# Copyright 2016 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# The forculus directory contains the implementation of the ForculusEncoder
# and the ForculusDecoder.

set_source_files_properties(${COBALT_PROTO_SRCS} PROPERTIES GENERATED TRUE)

add_library(forculus_encrypter forculus_encrypter.cc ${COBALT_PROTO_SRCS})
target_link_libraries(forculus_encrypter ${PROTOBUF_LIBRARY})

add_library(forculus_decrypter forculus_decrypter.cc ${COBALT_PROTO_SRCS})
target_link_libraries(forculus_decrypter ${PROTOBUF_LIBRARY})

# Build an executable test binary. Include gtest headers at compile time
# and gtest libraries at link time. Write the executable to the
# out/gtests directory
add_executable(forculus_encrypter_decrypter_test
               forculus_encrypter_decrypter_test.cc)
target_link_libraries(forculus_encrypter_decrypter_test
                      forculus_encrypter forculus_decrypter
                      gtest gtest_main)
set_target_properties(forculus_encrypter_decrypter_test PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})
