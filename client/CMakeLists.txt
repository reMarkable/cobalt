# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build file for the Cobalt client-side library.

add_library(metrics
            metrics.cc)
add_cobalt_dependencies(metrics)

# Build the tests
add_executable(client_tests
               metrics_test.cc)
target_link_libraries(client_tests metrics)
add_cobalt_test_dependencies(client_tests ${DIR_GTESTS})
