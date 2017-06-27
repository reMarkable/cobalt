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

# Generate the C++ and Go bindings for Shuffler's protocol buffers
set(SHUFFLER_PB_GO "${GO_PROTO_GEN_SRC_DIR}/shuffler/shuffler.pb.go")
set(CONFIG_PB_GO "${GO_PROTO_GEN_SRC_DIR}/shuffler/config.pb.go")
set(SHUFFLER_DB_PB_GO "${GO_PROTO_GEN_SRC_DIR}/shuffler/shuffler_db.pb.go")
set(ANALYZER_PB_GO "${GO_PROTO_GEN_SRC_DIR}/analyzer/analyzer_service/analyzer.pb.go")
add_custom_command(OUTPUT ${SHUFFLER_PB_GO}
                          ${CONFIG_PB_GO}
                          ${SHUFFLER_DB_PB_GO}
                          ${CMAKE_CURRENT_BINARY_DIR}/shuffler.grpc.pb.cc
                          ${CMAKE_CURRENT_BINARY_DIR}/shuffler.pb.cc
    COMMAND ${GO_PROTOC}
            ${CMAKE_CURRENT_SOURCE_DIR}/shuffler.proto
            ${CMAKE_CURRENT_SOURCE_DIR}/config.proto
            ${CMAKE_CURRENT_SOURCE_DIR}/shuffler_db.proto
            -I ${CMAKE_SOURCE_DIR}
            -I ${CMAKE_CURRENT_SOURCE_DIR}
            -I ${CMAKE_SOURCE_DIR}/third_party/go/src
            --go_out=plugins=grpc,Mobservation.proto=cobalt,Mencrypted_message.proto=cobalt:${GO_PROTO_GEN_SRC_DIR}
            --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`
            --grpc_out=${CMAKE_BINARY_DIR}
            --cpp_out=${CMAKE_BINARY_DIR}
    DEPENDS shuffler.proto
    DEPENDS shuffler_db.proto
    DEPENDS config.proto
    DEPENDS ${ENCRYPED_MESSAGE_PB_GO}
    DEPENDS ${OBSERVATION_PB_GO}
    DEPENDS ${ANALYZER_PB_GO}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Build shuffler executables
set(SHUFFLER_BIN "${CMAKE_BINARY_DIR}/shuffler/shuffler")
set(DISPATCHER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/dispatcher/dispatcher.go")
set(RECEIVER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/receiver/receiver.go")
set(CRYPTO_UTIL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/util/crypto_util.go")
set(ELLIPTIC_UTIL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/util/elliptic_util.go")
set(ENCRYPTED_MESSAGE_UTIL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/util/encrypted_message_util.go")
set(RAND_UTIL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/util/rand_util.go")
set(CONFIG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/shuffler_config/config_util.go")
set(STORE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/store.go")
set(MEM_STORE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/mem_store.go")
set(LEVELDB_STORE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/leveldb_store.go")
set(KEY_UTIL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/key_util.go")
set(STORE_ITER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/iter.go")
set(MEM_STORE_ITER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/mem_store_iter.go")
set(LEVELDB_STORE_ITER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/leveldb_store_iter.go")
add_custom_command(OUTPUT ${SHUFFLER_BIN}
    # Compiles shuffler_main and all its dependencies
    COMMAND ${GO_BIN} build -o ${SHUFFLER_BIN}
    DEPENDS ${SHUFFLER_PB_GO}
    DEPENDS ${SHUFFLER_DB_PB_GO}
    DEPENDS ${ANALYZER_PB_GO}
    DEPENDS ${CONFIG_PB_GO}
    DEPENDS ${CONFIG_SRC}
    DEPENDS ${DISPATCHER_SRC}
    DEPENDS ${RECEIVER_SRC}
    DEPENDS ${CRYPTO_UTIL_SRC}
    DEPENDS ${ELLIPTIC_UTIL_SRC}
    DEPENDS ${ENCRYPTED_MESSAGE_UTIL_SRC}
    DEPENDS ${RAND_UTIL_SRC}
    DEPENDS ${STORE_SRC}
    DEPENDS ${KEY_UTIL_SRC}
    DEPENDS ${STORE_ITER_SRC}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shuffler_main.go
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build the protobuf/gRPC c++ library for accessing the shuffler
add_library(shuffler_grpc_client shuffler.grpc.pb.cc shuffler.pb.cc)

# Build dispatcher unit tests
set(DISPATCHER_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/dispatcher/dispatcher_test.go")
set(DISPATCHER_TEST_BIN ${GO_TESTS}/dispatcher_test)
add_custom_command(OUTPUT ${DISPATCHER_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${DISPATCHER_TEST_BIN} ${DISPATCHER_TEST_SRC}
            ${DISPATCHER_SRC}
    DEPENDS ${DISPATCHER_SRC} ${DISPATCHER_TEST_SRC}
    DEPENDS ${SHUFFLER_PB_GO} ${ANALYZER_PB_GO}
    DEPENDS ${CONFIG_PB_GO} ${SHUFFLER_DB_PB_GO}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build receiver unit tests
set(RECEIVER_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/receiver/receiver_test.go")
set(RECEIVER_TEST_BIN ${GO_TESTS}/receiver_test)
add_custom_command(OUTPUT ${RECEIVER_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${RECEIVER_TEST_BIN} ${RECEIVER_TEST_SRC}
            ${RECEIVER_SRC}
    DEPENDS ${RECEIVER_SRC} ${RECEIVER_TEST_SRC}
    DEPENDS ${SHUFFLER_PB_GO} ${ANALYZER_PB_GO}
    DEPENDS ${CONFIG_PB_GO} ${SHUFFLER_DB_PB_GO}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build crypto util unit tests
set(CRYPTO_UTIL_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/util/crypto_util_test.go")
set(RAND_UTIL_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/util/rand_util_test.go")
set(UTIL_TEST_BIN ${GO_TESTS}/util_test)
add_custom_command(OUTPUT ${UTIL_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${UTIL_TEST_BIN} ${CRYPTO_UTIL_TEST_SRC} ${RAND_UTIL_TEST_SRC}
            ${CRYPTO_UTIL_SRC} ${ELLIPTIC_UTIL_SRC} ${RAND_UTIL_SRC}
    DEPENDS ${CRYPTO_UTIL_SRC} ${CRYPTO_UTIL_TEST_SRC} ${ELLIPTIC_UTIL_SRC}
            ${RAND_UTIL_SRC} ${RAND_UTIL_TEST_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build encrypted message util unit tests
set(ENCRYPTED_MESSAGE_UTIL_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/util/encrypted_message_util_test.go")
set(ENCRYPTED_MESSAGE_UTIL_TEST_BIN ${GO_TESTS}/encrypted_message_util_test)
add_custom_command(OUTPUT ${ENCRYPTED_MESSAGE_UTIL_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${ENCRYPTED_MESSAGE_UTIL_TEST_BIN} ${ENCRYPTED_MESSAGE_UTIL_TEST_SRC}
            ${ENCRYPTED_MESSAGE_UTIL_SRC} ${CRYPTO_UTIL_SRC} ${ELLIPTIC_UTIL_SRC} ${RAND_UTIL_SRC}
    DEPENDS ${ENCRYPTED_MESSAGE_UTIL_TEST_SRC}  ${ENCRYPTED_MESSAGE_UTIL_SRC}
    DEPENDS ${CRYPTO_UTIL_SRC} ${CRYPTO_UTIL_TEST_SRC} ${ELLIPTIC_UTIL_SRC}
            ${RAND_UTIL_SRC} ${RAND_UTIL_TEST_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build config unit tests
set(CONFIG_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/shuffler_config/config_util_test.go")
set(CONFIG_TEST_BIN ${GO_TESTS}/config_test)
add_custom_command(OUTPUT ${CONFIG_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${CONFIG_TEST_BIN} ${CONFIG_TEST_SRC} ${CONFIG_SRC}
    DEPENDS ${CONFIG_SRC} ${CONFIG_TEST_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build storage unit tests
set(STORE_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/store_test.go")
set(MEM_STORE_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/mem_store_test.go")
set(LEVELDB_STORE_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/leveldb_store_test.go")
set(TESTUTIL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/testutil.go")
set(KEY_UTIL_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/key_util_test.go")
set(STORAGE_TEST_BIN ${GO_TESTS}/storage_test)
add_custom_command(OUTPUT ${STORAGE_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${STORAGE_TEST_BIN}
        ${STORE_TEST_SRC} ${MEM_STORE_TEST_SRC} ${LEVELDB_STORE_TEST_SRC}
        ${STORE_SRC} ${MEM_STORE_SRC} ${LEVELDB_STORE_SRC} ${TESTUTIL_SRC}
        ${KEY_UTIL_TEST_SRC} ${KEY_UTIL_SRC}
        ${STORE_ITER_SRC} ${MEM_STORE_ITER_SRC} ${LEVELDB_STORE_ITER_SRC}
    DEPENDS ${STORE_SRC} ${STORE_TEST_SRC} ${TESTUTIL_SRC}
    DEPENDS ${MEM_STORE_SRC} ${MEM_STORE_TEST_SRC}
    DEPENDS ${LEVELDB_STORE_SRC} ${LEVELDB_STORE_TEST_SRC}
    DEPENDS ${KEY_UTIL_SRC} ${KEY_UTIL_TEST_SRC}
    DEPENDS ${STORE_ITER_SRC} ${MEM_STORE_ITER_SRC} ${LEVELDB_STORE_ITER_SRC}
    DEPENDS ${SHUFFLER_PB_GO} ${ANALYZER_PB_GO}
    DEPENDS ${CONFIG_PB_GO} ${SHUFFLER_DB_PB_GO}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Build shuffler target
add_custom_target(shuffler_all ALL
    DEPENDS ${SHUFFLER_BIN}
            ${TEST_CLIENT_BIN}
            ${DISPATCHER_TEST_BIN}
            ${RECEIVER_TEST_BIN}
            ${UTIL_TEST_BIN}
            ${ENCRYPTED_MESSAGE_UTIL_TEST_BIN}
            ${CONFIG_TEST_BIN}
            ${STORAGE_TEST_BIN}
)

# Generate protobuf files prior to compiling anything.  This is needed for
# parallel builds to work.  Almost everything depends on the cobalt package
# created by the shuffler_gen_pb target so do that first.
add_custom_target(shuffler_gen_pb
    DEPENDS ${SHUFFLER_PB_GO}
            ${ANALYZER_PB_GO}
            ${SHUFFLER_DB_PB_GO}
)

add_dependencies(shuffler_all shuffler_gen_pb)
