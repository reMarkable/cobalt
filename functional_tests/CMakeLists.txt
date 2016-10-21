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
set(ANALYZER_BIGTABLE_TEST_SRC
    "${FUNCTIONAL_TESTS_SRC}/analyzer_bigtable_test.go")
set(ANALYZER_BIGTABLE_TEST_BIN ${GO_TESTS}/analyzer_bigtable_test)

add_custom_command(OUTPUT ${ANALYZER_BIGTABLE_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${ANALYZER_BIGTABLE_TEST_BIN}
            ${ANALYZER_BIGTABLE_TEST_SRC}
    DEPENDS ${ANALYZER_BIGTABLE_TEST_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(test_all ALL
      DEPENDS ${ANALYZER_BIGTABLE_TEST_BIN}
)
