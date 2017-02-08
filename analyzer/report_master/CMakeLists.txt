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

# Compile report_master.proto and report_internal.proto
protobuf_generate_cpp(REPORT_PROTO_SRCS REPORT_PROTO_HDRS
                      report_master.proto report_internal.proto)

# generate the gRPC service.  CMake doesn't support it by default.
set(PROTO_GRPC "${CMAKE_CURRENT_BINARY_DIR}/report_master.grpc.pb.cc")
add_custom_command(OUTPUT ${PROTO_GRPC}
    COMMAND protoc ${CMAKE_CURRENT_SOURCE_DIR}/report_master.proto
                -I ${CMAKE_CURRENT_SOURCE_DIR}:${PROTOBUF_IMPORT_DIRS}
                --grpc_out=.
                --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`
    DEPENDS report_master.proto
)

set(REPORT_PROTO_SRCS ${REPORT_PROTO_SRCS} ${PROTO_GRPC})

# Runs protoc proactively on the proto files.
add_custom_target(build_report_protos
                  DEPENDS ${REPORT_PROTO_SRCS})

set_source_files_properties(${COBALT_PROTO_SRCS} PROPERTIES GENERATED TRUE)
set_source_files_properties(${CONFIG_PROTO_SRCS} PROPERTIES GENERATED TRUE)

# Build the ReportMaster library
add_library(analyzer_report_master_lib
            encoding_mixer.cc
            report_generator.cc
            ${COBALT_PROTO_SRCS} ${CONFIG_PROTO_SRCS} ${REPORT_PROTO_SRCS} )
target_link_libraries(analyzer_report_master_lib
                      analyzer_store
                      cobalt_crypto
                      forculus_analyzer
                      glog
                      gflags
                      rappor_analyzer
                      ${PROTOBUF_LIBRARY})

# Build the tests
add_executable(analyzer_report_master_tests
               encoding_mixer_test.cc
               report_generator_test.cc)
target_link_libraries(analyzer_report_master_tests
                      analyzer_report_master_lib encoder
                      gtest gtest_main)
set_target_properties(analyzer_report_master_tests PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})

# Versions of our tests that expect the existence of a running local instance of
# the Bigtable Emulator process.
add_executable(analyzer_report_master_emulator_tests
               report_generator_emulator_test.cc)
target_link_libraries(analyzer_report_master_emulator_tests
                      encoder analyzer_report_master_lib
                      gtest gtest_main)
set_target_properties(analyzer_report_master_emulator_tests PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS_BT_EMULATOR})

# Versions of our tests that connect to the real Cloud Bigtable. See
# notes in bigtabe_cloud_helper.h for how this works.
add_executable(analyzer_report_master_cloud_tests
               report_generator_cloud_test.cc)
target_link_libraries(analyzer_report_master_cloud_tests
                      encoder  analyzer_report_master_lib
                      gtest gtest_main)
set_target_properties(analyzer_report_master_cloud_tests PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS_CLOUD_BT})

