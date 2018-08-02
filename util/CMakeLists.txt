# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set_source_files_properties(${COBALT_PROTO_SRCS} PROPERTIES GENERATED TRUE)

add_library(datetime_util datetime_util.cc)
target_link_libraries(datetime_util)
add_cobalt_dependencies(datetime_util)

add_library(encrypted_message_util
            encrypted_message_util.cc
            ${COBALT_PROTO_SRCS})
target_link_libraries(encrypted_message_util
                      cobalt_crypto)
add_cobalt_dependencies(encrypted_message_util)

add_library(pem_util
            pem_util.cc)
target_link_libraries(pem_util
                      cobalt_crypto)
add_cobalt_dependencies(pem_util)

#add_executable(util_tests
#               datetime_util_test.cc
#               encrypted_message_util_test.cc)
#target_link_libraries(util_tests
#                      datetime_util encrypted_message_util)
#add_cobalt_test_dependencies(util_tests ${DIR_GTESTS})

add_subdirectory(clearcut)
add_subdirectory(crypto_util)
#add_subdirectory(gcs)

add_library(status
            status.cc
            status_codes.h
            status.h)
add_cobalt_dependencies(status)
