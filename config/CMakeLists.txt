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

add_subdirectory(config_parser)

# Generate the C++ bindings for the config .proto files.
# Also compile the generated C++ files into a static library.
cobalt_make_protobuf_cpp_lib(config_proto_lib
                             CONFIG_PROTO_HDRS
                             false
                             encodings metrics report_configs cobalt_config)

# Generate Go bindings for the config .proto files.
cobalt_protobuf_generate_go(generate_config_pb_go_files
                            CONFIG_PB_GO_FILES
                            false
                            cobalt_config encodings metrics report_configs)

# Build the analyzer_config library
add_library(analyzer_config
            analyzer_config.cc
            ${CONFIG_PROTO_HDRS})
target_link_libraries(analyzer_config
    config_proto_lib)
add_cobalt_dependencies(analyzer_config)

# Build the config_gen tool
add_executable(config_gen config_gen.cc ${CONFIG_PROTO_HDRS})
add_cobalt_dependencies(config_gen)

# Build the tests
add_executable(config_tests
               analyzer_config_test.cc
               config_test.cc
               ${CONFIG_PROTO_HDRS})
target_link_libraries(config_tests
                      analyzer_config)
add_cobalt_test_dependencies(config_tests ${DIR_GTESTS})
