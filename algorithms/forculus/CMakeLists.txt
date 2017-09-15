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

# The forculus directory contains the implementation of the ForculusEncoder
# and the ForculusDecoder.

set_source_files_properties(${COBALT_PROTO_HDRS} PROPERTIES GENERATED TRUE)
set_source_files_properties(${CONFIG_PROTO_HDRS} PROPERTIES GENERATED TRUE)

add_library(forculus_utils ${CONFIG_PROTO_HDRS}
            forculus_utils.cc polynomial_computations.cc field_element.cc)
target_link_libraries(forculus_utils
                      config_proto_lib
                      datetime_util)
add_cobalt_dependencies(forculus_utils)

add_library(forculus_encrypter forculus_encrypter.cc
            ${COBALT_PROTO_HDRS} ${CONFIG_PROTO_HDRS})
target_link_libraries(forculus_encrypter
                      client_secret forculus_utils
                      cobalt_proto_lib config_proto_lib)

add_library(forculus_analyzer forculus_analyzer.cc forculus_decrypter.cc
            ${COBALT_PROTO_HDRS} ${CONFIG_PROTO_HDRS})
target_link_libraries(forculus_analyzer
                      cobalt_proto_lib
                      config_proto_lib
                      forculus_utils)

# Build an executable test binary. Include gtest headers at compile time
# and gtest libraries at link time. Write the executable to the
# out/gtests directory
add_executable(forculus_tests
               field_element_test.cc
               polynomial_computation_test.cc
               forculus_encrypter_test.cc
               forculus_decrypter_test.cc
               forculus_analyzer_test.cc)
target_link_libraries(forculus_tests
                      forculus_encrypter forculus_analyzer)
add_cobalt_test_dependencies(forculus_tests ${DIR_GTESTS})

# Build performance test binary
add_executable(forculus_performance_test
               forculus_performance_test.cc)
target_link_libraries(forculus_performance_test
                      forculus_encrypter forculus_analyzer)
add_cobalt_test_dependencies(forculus_performance_test ${DIR_PERF_TESTS})
