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

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/encodings.pb.cc
                   OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/encodings.pb.h
                   OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/metrics.pb.cc
                   OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/metrics.pb.h
                   OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/report_configs.pb.cc
                   OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/report_configs.pb.h
    COMMAND protoc ${CMAKE_CURRENT_SOURCE_DIR}/encodings.proto
                   ${CMAKE_CURRENT_SOURCE_DIR}/metrics.proto
                   ${CMAKE_CURRENT_SOURCE_DIR}/report_configs.proto
        -I ${CMAKE_SOURCE_DIR}
        --cpp_out=${CMAKE_BINARY_DIR}
    DEPENDS encodings.proto
    DEPENDS metrics.proto
    DEPENDS report_configs.proto
)

# Generate Go bindings for the config .proto files.
set(ENCODINGS_PB_GO "${GO_PROTO_GEN_SRC_DIR}/config/encodings.pb.go")
set(METRICS_PB_GO "${GO_PROTO_GEN_SRC_DIR}/config/metrics.pb.go")
set(REPORT_CONFIGS_PB_GO "${GO_PROTO_GEN_SRC_DIR}/config/report_configs.pb.go")

add_custom_command(OUTPUT ${ENCODINGS_PB_GO} ${METRICS_PB_GO} ${REPORT_CONFIGS_PB_GO}
    COMMAND ${GO_PROTOC}
        ${CMAKE_CURRENT_SOURCE_DIR}/encodings.proto
        ${CMAKE_CURRENT_SOURCE_DIR}/metrics.proto
        ${CMAKE_CURRENT_SOURCE_DIR}/report_configs.proto
        -I ${CMAKE_SOURCE_DIR}
        --go_out=plugins=Mencodings.proto=config,Mmetrics.proto=config,Mreport_configs.proto=config:${GO_PROTO_GEN_SRC_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/encodings.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/metrics.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/report_configs.proto)

add_custom_target(build_config_go_protos
                  DEPENDS  ${REPORT_CONFIGS_PB_GO})

# Build the analyzer_config library
add_library(analyzer_config
            analyzer_config.cc
            ${CONFIG_PROTO_SRCS})

target_link_libraries(analyzer_config
                      glog
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
