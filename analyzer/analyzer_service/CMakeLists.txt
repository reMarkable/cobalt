# Copyright 2017 The Fuchsia Authors
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

# Defines the output of the command that runs protoc on the .proto files located
# in the report_master directory.

declare_proto_files_are_generated()

# Generate the C++ bindings for the analyzer gRPC service.
# Also compile the generated C++ files into a static library.
cobalt_make_protobuf_cpp_lib(analyzer_grpc_client
    ANALZER_SERVICE_PROTO_HDRS
    true
    analyzer)

# Generate the Go bindings for Analyzer gRPC service.
#set(use_grpc true)
#cobalt_protobuf_generate_go(generate_analyzer_pb_go_files
#                            ANALYZER_PB_GO_FILES
#                            use_grpc
#                            analyzer)
#add_dependencies(generate_analyzer_pb_go_files
#                 generate_config_pb_go_files
#                 generate_cobalt_pb_go_files)
#
## Build the analyzer-service library
#add_library(analyzer_service_lib
#            analyzer_service.cc
#            ${ANALZER_SERVICE_PROTO_HDRS}
#            ${CONFIG_PROTO_HDRS}
#            ${COBALT_PROTO_HDRS})
#target_link_libraries(analyzer_service_lib
#                      analyzer_grpc_client
#                      analyzer_store
#                      encrypted_message_util
#                      pem_util)
#add_cobalt_dependencies(analyzer_service_lib)
#
## Build the analyzer_service executable
#add_executable(analyzer_service main.cc)
#target_link_libraries(analyzer_service analyzer_service_lib)
#add_cobalt_dependencies(analyzer_service)
#
## Build the tests
#add_executable(analyzer_service_tests
#               ${CMAKE_SOURCE_DIR}/analyzer/store/memory_store.cc
#               analyzer_service_test.cc)
#target_link_libraries(analyzer_service_tests
#                      analyzer_service_lib)
#add_cobalt_test_dependencies(analyzer_service_tests ${DIR_GTESTS})
