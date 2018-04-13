# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build file for the Cobalt client-side library.

add_library(observations_collector
            observations_collector.cc)
add_cobalt_dependencies(observations_collector)

# Build the tests
add_executable(client_tests
               observations_collector_test.cc)
target_link_libraries(client_tests observations_collector)
add_cobalt_test_dependencies(client_tests ${DIR_GTESTS})
