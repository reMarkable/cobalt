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

set_source_files_properties(${YAMLPB_TEST_PB_GO} PROPERTIES GENERATED TRUE)

cobalt_protobuf_generate_go(generate_yamlpb_test_pb_go_files
                            YAMLPB_TEST_PB_GO
                            false
                            src/yamlpb/yamlpb_test)

set(YAMLPB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/yamlpb/yamlpb.go)
set(YAMLPB_TEST_BIN ${GO_TESTS}/yamlpb_test)
set(YAMLPB_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/yamlpb/yamlpb_test.go")
add_custom_command(OUTPUT ${YAMLPB_TEST_BIN}
    COMMAND ${GO_BIN} test -c -o ${YAMLPB_TEST_BIN} ${YAMLPB_TEST_SRC} ${YAMLPB_SRC}
    DEPENDS ${YAMLPB_SRC}
    DEPENDS ${YAMLPB_TEST_SRC}
    DEPENDS ${YAMLPB_TEST_PB_GO}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Force the build to occur.
add_custom_target(build_config_parser ALL
    DEPENDS ${YAMLPB_TEST_BIN}
)
