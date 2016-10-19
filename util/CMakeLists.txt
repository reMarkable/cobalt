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

add_library(datetime_util datetime_util.cc)

add_executable(datetime_util_test datetime_util_test.cc)
target_link_libraries(datetime_util_test gtest gtest_main datetime_util)
set_target_properties(datetime_util_test PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})

add_subdirectory(crypto_util)
