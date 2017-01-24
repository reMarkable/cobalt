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

# generate the bindings for shuffler proto
set(SHUFFLER_GEN_PROTO_FILE "${GO_PROTO_GEN}/cobalt/shuffler.pb.go")
set(ANALYZER_GEN_PROTO_FILE "${GO_PROTO_GEN}/cobalt/analyzer.pb.go")
set(CONFIG_GEN_PROTO_FILE "${GO_PROTO_GEN}/cobalt/config.pb.go")
add_custom_command(OUTPUT ${SHUFFLER_GEN_PROTO_FILE}
                          ${ANALYZER_GEN_PROTO_FILE}
                          ${CONFIG_GEN_PROTO_FILE}
    COMMAND ${PROTOC} ${CMAKE_CURRENT_SOURCE_DIR}/shuffler.proto
            ${CMAKE_SOURCE_DIR}/encrypted_message.proto
            ${CMAKE_SOURCE_DIR}/observation.proto
            ${CMAKE_SOURCE_DIR}/analyzer/analyzer.proto
            ${CMAKE_CURRENT_SOURCE_DIR}/config.proto
            -I ${CMAKE_SOURCE_DIR}
            -I ${CMAKE_CURRENT_SOURCE_DIR}
            -I ${CMAKE_SOURCE_DIR}/third_party/go/src
            --go_out=plugins=grpc:${GO_PROTO_GEN}/cobalt
            --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`
            --grpc_out=${CMAKE_BINARY_DIR}
            --cpp_out=${CMAKE_BINARY_DIR}
    # protoc-gen-go compiler automatically generates the files keeping the
    # directory structure intact in the final output. But this causes runtime
    # errors, so moving all generated files under cobalt package.
    COMMAND mv ${GO_PROTO_GEN}/cobalt/shuffler/shuffler.pb.go ${SHUFFLER_GEN_PROTO_FILE}
    COMMAND mv ${GO_PROTO_GEN}/cobalt/analyzer/analyzer.pb.go ${ANALYZER_GEN_PROTO_FILE}
    COMMAND mv ${GO_PROTO_GEN}/cobalt/shuffler/config.pb.go ${CONFIG_GEN_PROTO_FILE}
    COMMAND rmdir ${GO_PROTO_GEN}/cobalt/shuffler
    COMMAND rmdir ${GO_PROTO_GEN}/cobalt/analyzer
    DEPENDS shuffler.proto
    DEPENDS config.proto
    DEPENDS ${CMAKE_SOURCE_DIR}/encrypted_message.proto
    DEPENDS ${CMAKE_SOURCE_DIR}/observation.proto
    DEPENDS ${CMAKE_SOURCE_DIR}/analyzer/analyzer.proto
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Deploy shuffler config
set(SHUFFLER_CONF "${CMAKE_BINARY_DIR}/shuffler/conf/config_v0.txt")
set(SHUFFLER_CONF_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/config/config_v0.txt")

add_custom_command(OUTPUT ${SHUFFLER_CONF}
    COMMAND cp ${SHUFFLER_CONF_SRC} ${SHUFFLER_CONF}
    DEPENDS ${SHUFFLER_CONF_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build shuffler executables
set(SHUFFLER_BIN "${CMAKE_BINARY_DIR}/shuffler/shuffler")
set(DISPATCHER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/dispatcher/dispatcher.go")
set(RECEIVER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/receiver/receiver.go")
set(UTIL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/util/crypto_util.go")
set(CONFIG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/config/config_util.go")
set(STORE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/store.go")
set(MEM_STORE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/mem_store.go")
add_custom_command(OUTPUT ${SHUFFLER_BIN}
    # Compiles shuffler_main and all its dependencies
    COMMAND ${GO_BIN} build -o ${SHUFFLER_BIN} shuffler_main.go
    DEPENDS ${SHUFFLER_GEN_PROTO_FILE}
    DEPENDS ${ANALYZER_GEN_PROTO_FILE}
    DEPENDS ${CONFIG_GEN_PROTO_FILE}
    DEPENDS ${DISPATCHER_SRC}
    DEPENDS ${RECEIVER_SRC}
    DEPENDS ${UTIL_SRC}
    DEPENDS ${STORE_SRC}
    DEPENDS ${CONFIG_SRC}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shuffler_main.go
    DEPENDS ${SHUFFLER_CONF}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build the protobuf/gRPC c++ library for accessing the shuffler
set_source_files_properties(shuffler.grpc.pb.cc shuffler.pb.cc
                            PROPERTIES GENERATED TRUE)

add_library(cobalt_shuffler shuffler.grpc.pb.cc shuffler.pb.cc)

# Build shuffler test tools
set(TEST_CLIENT_BIN "${CMAKE_BINARY_DIR}/shuffler/shuffler_test_client")
add_custom_command(OUTPUT ${TEST_CLIENT_BIN}
    COMMAND ${GO_BIN} build -o ${TEST_CLIENT_BIN} tools/test_client.go
    DEPENDS src/tools/test_client.go
    DEPENDS ${SHUFFLER_BIN}
    DEPENDS ${SHUFFLER_GEN_PROTO_FILE}
    DEPENDS ${ANALYZER_GEN_PROTO_FILE}
    DEPENDS ${CONFIG_GEN_PROTO_FILE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build dispatcher unit tests
set(DISPATCHER_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/dispatcher/dispatcher_test.go")
set(DISPATCHER_TEST_BIN ${GO_TESTS}/dispatcher_test)
add_custom_command(OUTPUT ${DISPATCHER_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${DISPATCHER_TEST_BIN} ${DISPATCHER_TEST_SRC}
            ${DISPATCHER_SRC}
    DEPENDS ${DISPATCHER_SRC} ${DISPATCHER_TEST_SRC}
    DEPENDS ${SHUFFLER_GEN_PROTO_FILE} ${ANALYZER_GEN_PROTO_FILE}
    DEPENDS ${CONFIG_GEN_PROTO_FILE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build receiver unit tests
set(RECEIVER_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/receiver/receiver_test.go")
set(RECEIVER_TEST_BIN ${GO_TESTS}/receiver_test)
add_custom_command(OUTPUT ${RECEIVER_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${RECEIVER_TEST_BIN} ${RECEIVER_TEST_SRC}
            ${RECEIVER_SRC}
    DEPENDS ${RECEIVER_SRC} ${RECEIVER_TEST_SRC}
    DEPENDS ${SHUFFLER_GEN_PROTO_FILE} ${ANALYZER_GEN_PROTO_FILE}
    DEPENDS ${CONFIG_GEN_PROTO_FILE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build util unit tests
set(UTIL_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/util/crypto_util_test.go")
set(UTIL_TEST_BIN ${GO_TESTS}/crypto_util_test)
add_custom_command(OUTPUT ${UTIL_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${UTIL_TEST_BIN} ${UTIL_TEST_SRC} ${UTIL_SRC}
    DEPENDS ${UTIL_SRC} ${UTIL_TEST_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build config unit tests
set(CONFIG_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/config/config_util_test.go")
set(CONFIG_TEST_BIN ${GO_TESTS}/config_test)
add_custom_command(OUTPUT ${CONFIG_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${CONFIG_TEST_BIN} ${CONFIG_TEST_SRC} ${CONFIG_SRC}
    DEPENDS ${CONFIG_SRC} ${CONFIG_TEST_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build storage unit tests
set(STORAGE_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/mem_store_test.go")
set(STORAGE_TEST_BIN ${GO_TESTS}/storage_test)
add_custom_command(OUTPUT ${STORAGE_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${STORAGE_TEST_BIN} ${STORAGE_TEST_SRC} ${STORE_SRC} ${MEM_STORE_SRC}
    DEPENDS ${STORE_SRC} ${MEM_STORE_SRC} ${STORAGE_TEST_SRC}
    DEPENDS ${SHUFFLER_GEN_PROTO_FILE} ${ANALYZER_GEN_PROTO_FILE}
    DEPENDS ${CONFIG_GEN_PROTO_FILE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build shuffler target
add_custom_target(shuffler_all ALL
    DEPENDS ${SHUFFLER_BIN}
            ${SHUFFLER_CONF}
            ${TEST_CLIENT_BIN}
            ${DISPATCHER_TEST_BIN}
            ${RECEIVER_TEST_BIN}
            ${UTIL_TEST_BIN}
            ${CONFIG_TEST_BIN}
            ${STORAGE_TEST_BIN}
)

# Generate protobuf files prior to compiling anything.  This is needed for
# parallel builds to work.  Almost everything depends on the cobalt package
# created by the shuffler_gen_pb target so do that first.
add_custom_target(shuffler_gen_pb
    DEPENDS ${SHUFFLER_GEN_PROTO_FILE} ${ANALYZER_GEN_PROTO_FILE}
)

add_dependencies(shuffler_all shuffler_gen_pb)
