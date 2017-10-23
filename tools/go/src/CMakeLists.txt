# Copyright 2017 The Fuchsia Authors
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

set(REPORT_MASTER_PB_GO "${CMAKE_BINARY_DIR}/go-proto-gen/src/analyzer/report_master/report_master.pb.go")

set_source_files_properties(${REPORT_MASTER_PB_GO} PROPERTIES GENERATED TRUE)

# Build report client binary.
set(REPORT_CLIENT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/report_client/report_client.go"
                      "${CMAKE_CURRENT_SOURCE_DIR}/report_client/oauth.go")
set(REPORT_CLIENT_BINARY "${CMAKE_BINARY_DIR}/tools/report_client")
add_custom_command(OUTPUT ${REPORT_CLIENT_BINARY}
    # Compiles report_client_main and all its dependencies
    COMMAND ${GO_BIN} build -o ${REPORT_CLIENT_BINARY} report_client_main.go
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/report_client_main.go
    DEPENDS ${REPORT_CLIENT_SRC}
    DEPENDS ${REPORT_MASTER_PB_GO}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Build tests
set(TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/report_client/report_client_test.go")
set(TEST_BINARY ${GO_TESTS}/report_client_test)
add_custom_command(OUTPUT ${TEST_BINARY}
    COMMAND ${GO_BIN} test -c -o ${TEST_BINARY} ${TEST_SRC} ${REPORT_CLIENT_SRC}
    DEPENDS ${TEST_SRC}
    DEPENDS ${REPORT_CLIENT_SRC}
    DEPENDS ${REPORT_MASTER_PB_GO}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Force the build to occur.
add_custom_target(build_report_client ALL
    DEPENDS ${REPORT_CLIENT_BINARY} ${TEST_BINARY}
)
