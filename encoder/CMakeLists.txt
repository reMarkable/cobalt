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

set_source_files_properties(${COBALT_PROTO_HDRS} PROPERTIES GENERATED TRUE)
set_source_files_properties(${CONFIG_PROTO_HDRS} PROPERTIES GENERATED TRUE)

# client_secret library
add_library(client_secret client_secret.cc)
target_link_libraries(client_secret cobalt_crypto)
add_cobalt_dependencies(client_secret)

# encoder library
add_library(encoder
            encoder.cc
            envelope_maker.cc
            send_retryer.cc
            shipping_manager.cc
            shuffler_client.cc
            ${COBALT_PROTO_HDRS} ${CONFIG_PROTO_HDRS})
target_link_libraries(encoder
                      client_secret
                      encrypted_message_util
                      forculus_encrypter
                      rappor_encoder
                      shuffler_grpc_client)
add_cobalt_dependencies(encoder)

add_executable(encoder_tests
               client_secret_test.cc encoder_test.cc envelope_maker_test.cc
               send_retryer_test.cc shipping_manager_test.cc
               shuffler_client_test.cc)
target_link_libraries(encoder_tests
                      client_secret
                      encoder)
add_cobalt_test_dependencies(encoder_tests ${DIR_GTESTS})
