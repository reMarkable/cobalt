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

set(PROTOBUF_IMPORT_DIRS ${CMAKE_SOURCE_DIR})
protobuf_generate_cpp(CONFIG_PROTO_SRCS CONFIG_PROTO_HDRS
                      encodings.proto metrics.proto report_configs.proto)

# Build the analyzer_config library
add_library(analyzer_config
            analyzer_config.cc
            ${CONFIG_PROTO_SRCS})

target_link_libraries(analyzer_config
                      glog
                      gflags
                      ${PROTOBUF_LIBRARY})

# Build the config_gen tool
add_executable(config_gen config_gen.cc ${CONFIG_PROTO_SRCS})
target_link_libraries(config_gen ${PROTOBUF_LIBRARY})

# Build the tests
add_executable(config_tests
               analyzer_config_test.cc
               config_test.cc
               ${CONFIG_PROTO_SRCS})
target_link_libraries(config_tests
                      analyzer_config
                      gtest gtest_main
                      ${PROTOBUF_LIBRARY})
set_target_properties(config_tests PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})
