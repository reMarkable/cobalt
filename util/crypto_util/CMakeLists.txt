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

add_library(cobalt_cipher cipher.cc errors.cc random.cc)
target_link_libraries(cobalt_cipher crypto)


# Build an executable test binary. Include gtest headers at compile time
# and gtest libraries at link time. Write the executable to the
# out/gtests directory
add_executable(cobalt_cipher_test cipher_test.cc)
target_link_libraries(cobalt_cipher_test gtest gtest_main cobalt_cipher)
set_target_properties(cobalt_cipher_test PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/gtests)

