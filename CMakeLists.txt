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
set(PROTOBUF_LIBRARY /usr/lib/libprotobuf.so.10 grpc++)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})

# Run protoc on the top-level protos in order to generate C++ files for them.
find_package(Protobuf REQUIRED)
protobuf_generate_cpp(COBALT_PROTO_SRCS COBALT_PROTO_HDRS
                      cobalt.proto encrypted_message.proto encodings.proto
                      metrics.proto)
add_custom_target(build_protos, DEPENDS ${COBALT_PROTO_SRCS})

add_subdirectory(algorithms)
add_subdirectory(analyzer)
add_subdirectory(shuffler)
add_subdirectory(tools)

add_subdirectory(third_party/boringssl/src)
include_directories(BEFORE PRIVATE "${BoringSSL_SOURCE_DIR}/include")

add_subdirectory(third_party/googletest)
add_subdirectory(util/crypto_util)
