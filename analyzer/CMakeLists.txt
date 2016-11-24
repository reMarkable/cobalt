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

set(ANALYZER_SRCS
    analyzer_service.cc
    reporter.cc
    store/bigtable_store.cc
    store/mem_store.cc
    store/store.cc
    )

set(ANALYZER_LIBS
    ${PROTOBUF_LIBRARY}
    googleapis
    forculus_analyzer
    cobalt_crypto
    glog
    gflags
    )

set_source_files_properties(${COBALT_PROTO_SRCS} PROPERTIES GENERATED TRUE)
set_source_files_properties(${CONFIG_PROTO_SRCS} PROPERTIES GENERATED TRUE)

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

# Build the analyzer library
add_library(cobalt_analyzer ${ANALYZER_SRCS} ${PROTO_SRCS} ${COBALT_PROTO_SRCS})
target_link_libraries(cobalt_analyzer ${ANALYZER_LIBS})

# Build the analyzer executable
add_executable(analyzer main.cc)
target_link_libraries(analyzer cobalt_analyzer)

# Build the test
add_executable(analyzer_test analyzer_test.cc)
target_link_libraries(analyzer_test gtest gtest_main cobalt_analyzer)
set_target_properties(analyzer_test PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})
