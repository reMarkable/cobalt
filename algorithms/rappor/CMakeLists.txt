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

set_source_files_properties(${COBALT_PROTO_HDRS} PROPERTIES GENERATED TRUE)
set_source_files_properties(${CONFIG_PROTO_HDRS} PROPERTIES GENERATED TRUE)

add_library(rappor_config_validator rappor_config_validator.cc ${CONFIG_PROTO_HDRS})
target_link_libraries(rappor_config_validator
                      client_secret)
add_cobalt_dependencies(rappor_config_validator)

add_library(rappor_encoder rappor_encoder.cc
            ${COBALT_PROTO_HDRS} ${CONFIG_PROTO_HDRS})
target_link_libraries(rappor_encoder
                      client_secret cobalt_crypto rappor_config_validator)

add_library(rappor_analyzer
            basic_rappor_analyzer.cc bloom_bit_counter.cc rappor_analyzer.cc
            ${COBALT_PROTO_HDRS} ${CONFIG_PROTO_HDRS})
target_link_libraries(rappor_analyzer
                      rappor_encoder
                      rappor_config_validator
                      cobalt_crypto
                      lossmin_minimizers lossmin_loss_functions)

# The test depends directly on Boring SSL for the deterministic random.
include_directories(BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/third_party/boringssl/include")
add_executable(rappor_tests
               basic_rappor_analyzer_test.cc
               bloom_bit_counter_test rappor_encoder_test.cc rappor_analyzer_test.cc
               rappor_test_utils.cc rappor_test_utils_test.cc)
target_link_libraries(rappor_tests rappor_encoder rappor_analyzer rappor_analyzer)
add_cobalt_test_dependencies(rappor_tests ${DIR_GTESTS})
