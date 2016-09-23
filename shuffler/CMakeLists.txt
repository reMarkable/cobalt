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

SET (GO_TESTS "${CMAKE_BINARY_DIR}/go_tests")

file(MAKE_DIRECTORY ${GO_TESTS})

add_custom_target(shuffler ALL
    COMMAND go build -o ${CMAKE_BINARY_DIR}/shuffler/shuffler
    COMMAND go test -c -o ${GO_TESTS}/shuffler_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
