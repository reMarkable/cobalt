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

# Build the test_app library.
add_library(test_app test_app.cc)
target_link_libraries(test_app
                      analyzer_grpc_client
                      encoder
                      glog
                      pem_util)

# Build the cobalt_test_app executable.
add_executable(cobalt_test_app
               test_app_main.cc)
target_link_libraries(cobalt_test_app
                      test_app
                      glog)

# Build the tests
add_executable(test_app_tests
               test_app_test.cc)
target_link_libraries(test_app_tests
                      test_app
                      gtest gtest_main)
set_target_properties(test_app_tests PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})
