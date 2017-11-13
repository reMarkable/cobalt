// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include "util/gcs/gcs_util.h"

#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace util {
namespace gcs {

// The guts of this test have been commented out so that on our CI and CQ
// bots all we are doing is testing that GcsUtil compiles. A developer may
// uncomment the guts of the test and set the two environment variables
// appropriately in order to test uploading. Since GcsUtil is a very thin
// wrapper around google-api-cpp-client there is almost nothing we could
// mock out for a unit test.
TEST(GcUtilTest, SmokeTest) {
  GcsUtil gcs_util;
  /*
   setenv("GRPC_DEFAULT_SSL_ROOTS_FILE_PATH",
          "<cobalt_root_dir>/third_party/grpc/etc/roots.pem", 1);
   setenv("GOOGLE_APPLICATION_CREDENTIALS",
          "<path to some service account key file>",
          1);
   ASSERT_TRUE(gcs_util.InitFromDefaultPaths());
   std::string data("Glory glory hallelulyah!");
   //ASSERT_TRUE(gcs_util.Ping("fuchsia-cobalt-testing"));
   ASSERT_TRUE(gcs_util.Upload("rudominer-cobalt-test-1", "glory", "text/plain",
                               data.data(), data.size()));
   ASSERT_TRUE(gcs_util.Upload("rudominer-cobalt-test-1", "glory2",
   "text/plain",
                               data.data(), data.size()));
   ASSERT_TRUE(gcs_util.Ping("fuchsia-cobalt-testing"));
   ASSERT_TRUE(gcs_util.Ping("fuchsia-cobalt-testing"));
   */
}

}  // namespace gcs
}  // namespace util
}  // namespace cobalt
