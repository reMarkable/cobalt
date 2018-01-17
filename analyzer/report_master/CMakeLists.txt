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

declare_proto_files_are_generated()

# Generate the C++ bindings for the ReportMaster gRPC service.
# Also compile the generated C++ files into a static library.
cobalt_make_protobuf_cpp_lib(report_master_proto_lib
    REPORT_PROTO_HDRS
    true
    report_master report_internal)

# Generate the Go bindings for ReportMaster gRPC service.
cobalt_protobuf_generate_go(generate_report_master_pb_go_files
                            REPORT_MASTER_PB_GO_FILES
                            true
                            report_master)
add_dependencies(generate_report_master_pb_go_files
                 generate_config_pb_go_files
                 generate_cobalt_pb_go_files)

# Generate the descriptor set to configure cloud endpoints for the ReportMaster service.
cobalt_generate_protobuf_descriptors(generate_report_master_descriptor
                                     REPORT_MASTER_PROTO_DESCRIPTOR
                                     report_master)

# Build the ReportMaster library
add_library(analyzer_report_master_lib
            auth_enforcer.cc
            histogram_analysis_engine.cc
            raw_dump_reports.cc
            report_executor.cc
            report_exporter.cc
            report_generator.cc
            report_history_cache.cc
            report_master_service.cc
            report_row_iterator.cc
            report_scheduler.cc
            report_serializer.cc
            report_stream.cc
            ${COBALT_PROTO_HDRS} ${CONFIG_PROTO_HDRS} ${REPORT_PROTO_HDRS} )
target_link_libraries(analyzer_report_master_lib
                      analyzer_config
                      analyzer_store
                      cobalt_crypto
                      gcs_util
                      pem_util
                      forculus_analyzer
                      rappor_analyzer
                      report_master_proto_lib)
add_cobalt_dependencies(analyzer_report_master_lib)

# Build the Report Master executable
add_executable(analyzer_report_master main.cc)
target_link_libraries(analyzer_report_master
                      analyzer_report_master_lib)
add_cobalt_dependencies(analyzer_report_master)

# Build the tests
add_executable(analyzer_report_master_tests
               ${CMAKE_SOURCE_DIR}/analyzer/store/memory_store.cc
               auth_enforcer_test.cc
               histogram_analysis_engine_test.cc
               raw_dump_reports_test.cc
               report_executor_test.cc
               report_exporter_test.cc
               report_generator_test.cc
               report_history_cache_test.cc
               report_row_iterator_test.cc
               report_scheduler_test.cc
               report_master_service_test.cc
               report_serializer_test.cc
               report_stream_test.cc)
target_link_libraries(analyzer_report_master_tests
                      analyzer_report_master_lib
                      encoder
                      report_store_testutils)
add_cobalt_test_dependencies(analyzer_report_master_tests ${DIR_GTESTS})

# Versions of our tests that expect the existence of a running local instance of
# the Bigtable Emulator process.
add_executable(analyzer_report_master_emulator_tests
               report_executor_emulator_test.cc
               report_generator_emulator_test.cc
               report_master_service_emulator_test.cc)
target_link_libraries(analyzer_report_master_emulator_tests
                      analyzer_report_master_lib
                      encoder
                      report_store_testutils)
add_cobalt_test_dependencies(analyzer_report_master_emulator_tests ${DIR_GTESTS_BT_EMULATOR})

# Versions of our tests that connect to the real Cloud Bigtable. See
# notes in bigtabe_cloud_helper.h for how this works.
add_executable(analyzer_report_master_cloud_tests
               report_executor_cloud_test.cc
               report_generator_cloud_test.cc
               report_master_service_cloud_test.cc)
target_link_libraries(analyzer_report_master_cloud_tests
                      analyzer_report_master_lib
                      encoder
                      report_store_testutils)
add_cobalt_test_dependencies(analyzer_report_master_cloud_tests ${DIR_GTESTS_CLOUD_BT})
