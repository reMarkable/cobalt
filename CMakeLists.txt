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

project(cobalt)

SET (CMAKE_C_COMPILER "/usr/bin/clang")
SET (CMAKE_CXX_COMPILER "/usr/bin/clang++")
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -std=c++11 -stdlib=libstdc++ -fcolor-diagnostics")

SET (DIR_GTESTS "${CMAKE_BINARY_DIR}/gtests")

# Note - we specify the exact library version because dev environments have
# protobuf.so.8 (v2) preinstalled in a higher priority search path.  Instead we
# want to link against protobuf v3
set(PROTOBUF_LIBRARY /usr/lib/libprotobuf.so.10 grpc++ grpc)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})

# Run protoc on the top-level protos in order to generate C++ files for them.
find_package(Protobuf REQUIRED)
protobuf_generate_cpp(COBALT_PROTO_SRCS COBALT_PROTO_HDRS
                      encodings.proto encrypted_message.proto observation.proto
                      metrics.proto)
add_custom_target(build_protos, DEPENDS ${COBALT_PROTO_SRCS})

# Go related defines
set(GO_PATH env GOPATH="${CMAKE_SOURCE_DIR}/third_party/go:${CMAKE_BINARY_DIR}/go-proto-gen:/usr/local/cobalt/go:${CMAKE_SOURCE_DIR}/shuffler")
set(GO_BIN ${GO_PATH} /usr/local/go/bin/go)
set(GO_TESTS "${CMAKE_BINARY_DIR}/go_tests")

file(MAKE_DIRECTORY ${GO_TESTS})

# Project directories
add_subdirectory(algorithms)
add_subdirectory(analyzer)
add_subdirectory(config)
add_subdirectory(encoder)
add_subdirectory(functional_tests)
add_subdirectory(shuffler)
add_subdirectory(tools)
add_subdirectory(third_party/boringssl/src)
add_subdirectory(third_party/googletest)
add_subdirectory(util)
