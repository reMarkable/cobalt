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

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS analyzer.proto)

# generate the gRPC service.  CMake doesn't support it by default.
set(PROTO_GRPC "${CMAKE_CURRENT_BINARY_DIR}/analyzer.grpc.pb.cc")
add_custom_command(OUTPUT ${PROTO_GRPC}
    COMMAND protoc ${CMAKE_CURRENT_SOURCE_DIR}/analyzer.proto
                -I ${CMAKE_CURRENT_SOURCE_DIR}:${PROTOBUF_IMPORT_DIRS}
                --grpc_out=.
                --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`
    DEPENDS analyzer.proto
)

set(PROTO_SRCS ${PROTO_SRCS} ${PROTO_GRPC})

set_source_files_properties(${COBALT_PROTO_SRCS} PROPERTIES GENERATED TRUE)
set_source_files_properties(${CONFIG_PROTO_SRCS} PROPERTIES GENERATED TRUE)

# Build the analyzer-service library
add_library(analyzer_service_lib
            analyzer_service.cc
            ${COBALT_PROTO_SRCS} ${CONFIG_PROTO_SRCS} ${PROTO_SRCS})
target_link_libraries(analyzer_service_lib
                      analyzer_store
                      cobalt_crypto
                      glog
                      gflags
                      ${PROTOBUF_LIBRARY})

# Build the analyzer_service executable
add_executable(analyzer_service main.cc)
target_link_libraries(analyzer_service analyzer_service_lib)

# Build the tests
add_executable(analyzer_service_tests
               analyzer_service_test.cc)
target_link_libraries(analyzer_service_tests
                      analyzer_service_lib
                      gtest gtest_main)
set_target_properties(analyzer_service_tests PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})
