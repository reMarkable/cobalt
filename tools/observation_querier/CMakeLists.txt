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

# Build the observation_querier library.
add_library(observation_querier observation_querier.cc)
target_link_libraries(observation_querier
                      analyzer_store
                      glog)

# Build the query_observations executable.
add_executable(query_observations
               observation_querier_main.cc)
target_link_libraries(query_observations
                      observation_querier
                      glog)

# Build the tests
add_executable(observation_querier_tests
               observation_querier_test.cc)
target_link_libraries(observation_querier_tests
                      observation_querier
                      gtest gtest_main)
set_target_properties(observation_querier_tests PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})
