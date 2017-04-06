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
set_source_files_properties(${CONFIG_PROTO_SRCS} PROPERTIES GENERATED TRUE)

# client_secret library
add_library(client_secret client_secret.cc)
target_link_libraries(client_secret cobalt_crypto)

# encoder library
add_library(encoder
            encoder.cc
            envelope_maker.cc
            shuffler_client.cc
            ${COBALT_PROTO_SRCS} ${CONFIG_PROTO_SRCS})
target_link_libraries(encoder
                      client_secret
                      encrypted_message_util
                      forculus_encrypter
                      glog
                      rappor_encoder
                      shuffler_grpc_client)

add_executable(encoder_tests
               client_secret_test.cc
               encoder_test.cc envelope_maker_test.cc shuffler_client_test)
target_link_libraries(encoder_tests
                      gflags gtest gtest_main client_secret encoder
                      ${PROTOBUF_LIBRARY})
set_target_properties(encoder_tests PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})
