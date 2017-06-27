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

cmake_minimum_required (VERSION 2.8.10)

# The only way we found to override the compiler is setting these variables
# prior to the project definition.  Otherwise enable_language() sets these
# variables and gcc will be picked up instead.  Also, new versions of cmake will
# complain if one compiler is first set (say to gcc via enable_language), and
# then we later switch it to (say) clang.
SET (CMAKE_C_COMPILER "clang")
SET (CMAKE_CXX_COMPILER "clang++")

# Tell the file logging.h that we want to use Google's GLog library for
# logging.
add_definitions(-DHAVE_GLOG=1)

project(cobalt)

SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Wsign-compare -Wignored-qualifiers -std=c++11 -stdlib=libstdc++ -fcolor-diagnostics")

enable_language(C)
enable_language(CXX)

SET (DIR_GTESTS "${CMAKE_BINARY_DIR}/gtests")
SET (DIR_SYSROOT "${CMAKE_SOURCE_DIR}/sysroot")
SET (DIR_END_TO_END_TESTS "${CMAKE_BINARY_DIR}/e2e_tests")
SET (DIR_GTESTS_BT_EMULATOR "${CMAKE_BINARY_DIR}/gtests_btemulator")
SET (DIR_GTESTS_CLOUD_BT "${CMAKE_BINARY_DIR}/gtests_cloud_bt")
SET (DIR_PERF_TESTS "${CMAKE_BINARY_DIR}/perf_tests")

# Note - we specify the exact library version because dev environments have
# protobuf.so.8 (v2) preinstalled in a higher priority search path.  Instead we
# want to link against protobuf v3
set(PROTOBUF_LIBRARY ${DIR_SYSROOT}/lib/libprotobuf.so.10 grpc++ grpc)
set(PROTOBUF_IMPORT_DIRS ${CMAKE_SOURCE_DIR})

# If using protoc installed by setup.sh, cmake will warn us that it doesn't know
# how to generate protoc (because it lives in the out directory).  Suppress that
# warning.
if(POLICY CMP0058)
    cmake_policy(SET CMP0058 NEW)
endif()

link_directories(${DIR_SYSROOT}/lib)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})
include_directories(BEFORE SYSTEM ${DIR_SYSROOT}/include)

# Defines a command to run protoc on the top-level .proto files.
find_package(Protobuf REQUIRED QUIET)
protobuf_generate_cpp(COBALT_PROTO_SRCS COBALT_PROTO_HDRS
                      encrypted_message.proto observation.proto)

# Defines the output of the command that runs protoc on the .proto files located
# in the config directory.
set(CONFIG_PROTO_SRCS
    "${CMAKE_BINARY_DIR}/config/encodings.pb.cc"
    "${CMAKE_BINARY_DIR}/config/metrics.pb.cc"
    "${CMAKE_BINARY_DIR}/config/report_configs.pb.cc")

# Runs protoc proactively on the proto files in the top-level and config dirs.
add_custom_target(build_protos
                  DEPENDS ${COBALT_PROTO_SRCS} ${CONFIG_PROTO_SRCS})

# Go related defines
set(GO_PATH env GOPATH="${DIR_SYSROOT}/go:${CMAKE_SOURCE_DIR}/third_party/go:${CMAKE_BINARY_DIR}/go-proto-gen")
set(GO_PATH env "${GO_PATH}:${CMAKE_SOURCE_DIR}/shuffler")
set(GO_PATH env "${GO_PATH}:${CMAKE_SOURCE_DIR}/tools/go")
set(GO_BIN ${GO_PATH} go)
set(GO_TESTS "${CMAKE_BINARY_DIR}/go_tests")

file(MAKE_DIRECTORY ${GO_TESTS})

# Generate Go bindings for some of the top-level .proto files.
set(GO_PROTOC ${GO_PATH} protoc)
set(GO_PROTO_GEN_SRC_DIR "${CMAKE_BINARY_DIR}/go-proto-gen/src")
file(MAKE_DIRECTORY ${GO_PROTO_GEN_SRC_DIR})
set(ENCRYPTED_MESSAGE_PB_GO "${GO_PROTO_GEN_SRC_DIR}/cobalt/encrypted_message.pb.go")
set(OBSERVATION_PB_GO "${GO_PROTO_GEN_SRC_DIR}/cobalt/observation.pb.go")

add_custom_command(OUTPUT ${ENCRYPTED_MESSAGE_PB_GO} ${OBSERVATION_PB_GO}
    COMMAND ${GO_PROTOC}
        ${CMAKE_CURRENT_SOURCE_DIR}/encrypted_message.proto
        ${CMAKE_CURRENT_SOURCE_DIR}/observation.proto
        -I ${CMAKE_SOURCE_DIR}
        --go_out=plugins=Mobservation.proto=cobalt,Mencrypted_message.proto=cobalt:${GO_PROTO_GEN_SRC_DIR}/cobalt
    DEPENDS ${CMAKE_SOURCE_DIR}/encrypted_message.proto
    DEPENDS ${CMAKE_SOURCE_DIR}/observation.proto
)

add_custom_target(build_go_protos
                  DEPENDS  ${ENCRYPTED_MESSAGE_PB_GO} ${OBSERVATION_PB_GO})

# Project directories
add_subdirectory(algorithms)
add_subdirectory(analyzer)
add_subdirectory(config)
add_subdirectory(encoder)
add_subdirectory(end_to_end_tests)
add_subdirectory(shuffler)
add_subdirectory(tools)
add_subdirectory(util)

# Turn off extra warnings on third_party code we don't control.
add_compile_options(-Wno-sign-compare -Wno-ignored-qualifiers)
add_subdirectory(third_party/boringssl)
add_subdirectory(third_party/googletest)

add_compile_options(-Wno-deprecated-register -Wno-unused-function -Wno-unused-local-typedef)
add_subdirectory(third_party/glog)
