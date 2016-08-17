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

# A dummy utility library. Just a place-holder to test the Cobalt
# build and test framework.
add_library(dummy_util dummy_util.cc)

# Build an executable test binary. Include gtest headers at compile time
# and gtest libraries at link time. Write the executable to the
# out/gtests directory
add_executable(dummy_util_test dummy_util_test.cc)
target_include_directories(dummy_util_test PRIVATE
	                       "${gtest_SOURCE_DIR}/include")
target_link_libraries(dummy_util_test gtest gtest_main dummy_util)
set_target_properties(dummy_util_test PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/gtests)


# This dummy executable tests that we are able to link against Boring SSL.
# Include the Boring SSL include directory at build time and the "crypto"
# library at link time. Write the executable to the out/gtests directory
# even though the executable is not really a gtest.
add_executable(boring_ssl_test boring_ssl_test.cc)
target_include_directories(boring_ssl_test PRIVATE
	                       "${BoringSSL_SOURCE_DIR}/include")
target_link_libraries(boring_ssl_test crypto)
set_target_properties(boring_ssl_test PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/gtests)

