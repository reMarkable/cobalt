# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cobalt_make_protobuf_cpp_lib(clearcut_proto_lib
                             CLEARCUT_PROTO_HDRS
                             false
                             clearcut)

cobalt_protobuf_generate_go(generate_clearcut_pb_go_files
                            CLEARCUT_PB_GO_FILES
                            false
                            clearcut)
add_library(clearcut
            http_client.h
            uploader.h
            uploader.cc)
add_cobalt_dependencies(clearcut)
target_link_libraries(clearcut clearcut_proto_lib tensorflow_statusor status)

add_executable(clearcut_tests
               uploader_test.cc)
target_link_libraries(clearcut_tests clearcut)
add_cobalt_test_dependencies(clearcut_tests ${DIR_GTESTS})
