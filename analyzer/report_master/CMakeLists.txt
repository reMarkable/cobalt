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

# Compile report_master.proto and report_internal.proto
protobuf_generate_cpp(REPORT_PROTO_SRCS REPORT_PROTO_HDRS
                      report_master.proto report_internal.proto)

# generate the gRPC service.  CMake doesn't support it by default.
set(PROTO_GRPC "${CMAKE_CURRENT_BINARY_DIR}/report_master.grpc.pb.cc")
add_custom_command(OUTPUT ${PROTO_GRPC}
    COMMAND protoc ${CMAKE_CURRENT_SOURCE_DIR}/report_master.proto
                -I ${CMAKE_CURRENT_SOURCE_DIR}:${PROTOBUF_IMPORT_DIRS}
                --grpc_out=.
                --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`
    DEPENDS report_master.proto
)

set(REPORT_PROTO_SRCS ${REPORT_PROTO_SRCS} ${PROTO_GRPC})

# Runs protoc proactively on the proto files.
add_custom_target(build_report_protos
                  DEPENDS ${REPORT_PROTO_SRCS})
