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

set_source_files_properties(${COBALT_PROTO_SRCS} PROPERTIES GENERATED TRUE)

add_library(datetime_util datetime_util.cc)

add_library(encrypted_message_util
            encrypted_message_util.cc
            ${COBALT_PROTO_SRCS})
target_link_libraries(encrypted_message_util
                      cobalt_crypto
                      glog
                      ${PROTOBUF_LIBRARY})

add_library(pem_util
            pem_util.cc)
target_link_libraries(pem_util
                      cobalt_crypto
                      glog)

add_executable(util_tests
               datetime_util_test.cc
               encrypted_message_util_test.cc)
target_link_libraries(util_tests
                      gtest gtest_main
                      datetime_util encrypted_message_util)
set_target_properties(util_tests PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})

add_subdirectory(crypto_util)
