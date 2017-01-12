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

set_source_files_properties(${COBALT_PROTO_SRCS} PROPERTIES GENERATED TRUE)
set_source_files_properties(${CONFIG_PROTO_SRCS} PROPERTIES GENERATED TRUE)

# Build the analyzer store library
add_library(analyzer_store
            bigtable_store_old.cc
            data_store.cc
            mem_store.cc
            memory_store.cc
            observation_store.cc
            store.cc
            ${PROTO_SRCS} ${COBALT_PROTO_SRCS})

target_link_libraries(analyzer_store
                      cobalt_crypto
                      glog
                      gflags
                      googleapis
                      ${PROTOBUF_LIBRARY})

# Build the test
add_executable(analyzer_store_tests
               memory_store_test.cc observation_store_test.cc)
target_link_libraries(analyzer_store_tests gtest gtest_main analyzer_store)
set_target_properties(analyzer_store_tests PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${DIR_GTESTS})
