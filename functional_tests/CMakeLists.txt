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

# Build functional tests
set(FUNCTIONAL_TESTS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(COBALT_TEST_SRC
    "${FUNCTIONAL_TESTS_SRC}/cobalt_test.go")
set(COBALT_TEST_BIN ${GO_TESTS}/cobalt_test)

set(COBALT_SHUFFLER_CONF_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/config/shuffler_default.conf")
set(COBALT_TEST_CONF_BIN ${CMAKE_BINARY_DIR}/config/shuffler_default.conf)

add_custom_command(OUTPUT ${COBALT_TEST_CONF_BIN}
    COMMAND cp ${COBALT_SHUFFLER_CONF_SRC} ${CMAKE_BINARY_DIR}/config
    DEPENDS ${COBALT_SHUFFLER_CONF_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_command(OUTPUT ${COBALT_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${COBALT_TEST_BIN}
            ${COBALT_TEST_SRC}
    DEPENDS ${COBALT_TEST_SRC}
    DEPENDS ${COBALT_SHUFFLER_CONF_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(test_all ALL
      DEPENDS ${COBALT_TEST_BIN}
      DEPENDS ${COBALT_TEST_CONF_BIN}
)

add_executable(bigtable_test ${FUNCTIONAL_TESTS_SRC}/bigtable_test.cc)
target_link_libraries(bigtable_test gtest gtest_main cobalt_analyzer)
set_target_properties(bigtable_test PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})
