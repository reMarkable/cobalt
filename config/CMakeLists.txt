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

set_source_files_properties(${COBALT_PROTO_SRCS} PROPERTIES GENERATED TRUE)

# Build the config_gen tool
add_executable(config_gen config_gen.cc ${COBALT_PROTO_SRCS})
target_link_libraries(config_gen ${PROTOBUF_LIBRARY})

# Build the test
add_executable(config_test config_test.cc ${COBALT_PROTO_SRCS})
target_link_libraries(config_test gtest gtest_main ${PROTOBUF_LIBRARY})
set_target_properties(config_test PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})
