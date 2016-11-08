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

# The rappor directory contains the implementation of the RAPPOR encoder
# (and will in the future contain the RAPPOR analysis code also.)

set_source_files_properties(${COBALT_PROTO_SRCS} PROPERTIES GENERATED TRUE)
set_source_files_properties(${CONFIG_PROTO_SRCS} PROPERTIES GENERATED TRUE)

add_library(rappor_encoder rappor_encoder.cc
            ${COBALT_PROTO_SRCS} ${CONFIG_PROTO_SRCS})
target_link_libraries(rappor_encoder
                      client_secret cobalt_crypto ${PROTOBUF_LIBRARY})

# The test depends directly on Boring SSL for the deterministic random.
include_directories(BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/third_party/boringssl/src/include")
add_executable(rappor_encoder_test rappor_encoder_test.cc)
target_link_libraries(rappor_encoder_test rappor_encoder gtest gtest_main)
set_target_properties(rappor_encoder_test PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})
