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

# Defines the output of the command that runs protoc on the .proto files located
# in the report_master directory.
set(REPORT_PROTO_SRCS
    "${CMAKE_BINARY_DIR}/analyzer/report_master/report_master.pb.cc"
    "${CMAKE_BINARY_DIR}/analyzer/report_master/report_internal.pb.cc")

add_subdirectory(analyzer_service)
#add_subdirectory(report_master)
#add_subdirectory(store)
