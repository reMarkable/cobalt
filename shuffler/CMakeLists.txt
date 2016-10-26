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

set(GO_PROTO_GEN "${CMAKE_BINARY_DIR}/go-proto-gen/src")
set(PROTOC ${GO_PATH} protoc)

file(MAKE_DIRECTORY ${GO_PROTO_GEN})

# generate the go bindings for shuffler proto
set(SHUFFLER_GEN_PROTO_FILE "${GO_PROTO_GEN}/shuffler/shuffler.pb.go")
set(EM_GEN_PROTO_FILE "${GO_PROTO_GEN}/encrypted_message.pb.go")
add_custom_command(OUTPUT ${SHUFFLER_GEN_PROTO_FILE}
    COMMAND ${PROTOC} ${CMAKE_CURRENT_SOURCE_DIR}/shuffler.proto
            ${CMAKE_SOURCE_DIR}/encrypted_message.proto
            -I ${CMAKE_SOURCE_DIR}
            -I ${CMAKE_CURRENT_SOURCE_DIR}
            -I ${CMAKE_SOURCE_DIR}/third_party/go/src
            --go_out=plugins=grpc:${GO_PROTO_GEN}
    COMMAND mv ${EM_GEN_PROTO_FILE} ${GO_PROTO_GEN}/shuffler
    DEPENDS shuffler.proto ${CMAKE_SOURCE_DIR}/encrypted_message.proto
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Build shuffler executables
set(SHUFFLER_BIN "${CMAKE_BINARY_DIR}/shuffler/shuffler")
add_custom_command(OUTPUT ${SHUFFLER_BIN}
    COMMAND ${GO_BIN} build dispatcher
    COMMAND ${GO_BIN} build receiver
    # Compiles all dependencies and installs main service package
    COMMAND ${GO_BIN} build -o ${SHUFFLER_BIN} shuffler_main.go
    DEPENDS ${SHUFFLER_GEN_PROTO_FILE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build shuffler test tools
set(TEST_CLIENT_BIN "${CMAKE_BINARY_DIR}/shuffler/shuffler_test_client")
add_custom_command(OUTPUT ${TEST_CLIENT_BIN}
    COMMAND ${GO_BIN} build -o ${TEST_CLIENT_BIN} tools/test_client.go
    DEPENDS src/tools/test_client.go
    DEPENDS ${SHUFFLER_GEN_PROTO_FILE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build shuffler functional tests
set(DISPATCHER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/dispatcher/dispatcher.go")
set(DISPATCHER_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/dispatcher/dispatcher_test.go")
set(DISPATCHER_TEST_BIN ${GO_TESTS}/dispatcher_test)
add_custom_command(OUTPUT ${DISPATCHER_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${DISPATCHER_TEST_BIN} ${DISPATCHER_TEST_SRC}
            ${DISPATCHER_SRC}
    DEPENDS ${DISPATCHER_SRC} ${DISPATCHER_TEST_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build util functional tests
set(UTIL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/util/crypto_util.go")
set(UTIL_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/util/crypto_util_test.go")
set(UTIL_TEST_BIN ${GO_TESTS}/crypto_util_test)
add_custom_command(OUTPUT ${UTIL_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${UTIL_TEST_BIN} ${UTIL_TEST_SRC} ${UTIL_SRC}
    DEPENDS ${UTIL_SRC} ${UTIL_TEST_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build shuffler target
add_custom_target(shuffler_all ALL
    DEPENDS ${SHUFFLER_BIN}
            ${TEST_CLIENT_BIN}
            ${DISPATCHER_TEST_BIN}
            ${UTIL_TEST_BIN}
)
