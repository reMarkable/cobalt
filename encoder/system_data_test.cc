// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encoder/system_data.h"

#include <set>
#include <string>
#include <utility>

#include "./gtest.h"
#include "./logging.h"

namespace cobalt {
namespace encoder {

TEST(SystemDataTest, BasicTest) {
  SystemData system_data;
  EXPECT_NE(SystemProfile::UNKNOWN_OS, system_data.system_profile().os());
  EXPECT_NE(SystemProfile::UNKNOWN_ARCH, system_data.system_profile().arch());

  // TODO(rudominer) Re-enable this when we have implemented
  // PopulateCpuInfo() on ARM.
  /*
  EXPECT_TRUE(system_data.system_profile().has_cpu());

  // Test the vendor_name.
  // NOTE(rudominer): Each time this test fails on a new machine please
  // add the found vendor_name to this list.
  std::set<std::string> expected_vendor_names = {"GenuineIntel"};
  std::string vendor_name = system_data.system_profile().cpu().vendor_name();
  EXPECT_TRUE(expected_vendor_names.count(vendor_name)) << "found vendor_name="
                                                        << vendor_name;

  // Test the cpu signature.
  std::set<int> expected_signatures = {
      0x0306D4,  // Intel Broadwell (model=0x3D family=0x6) stepping=0x4
      0x0306F0,  // Intel Broadwell (model=0x3F family=0x6) stepping=0x0
      0x0406E3,  // Intel Broadwell (model=0x4E family=0x6) stepping=0x3
      0x0406F1,  // Intel Broadwell (model=0x4F family=0x6) stepping=0x1
  };
  int signature = system_data.system_profile().cpu().signature();
  if (expected_signatures.count(signature) == 0) {
    LOG(WARNING) << "***** found new signature: " << signature;
  }
  EXPECT_GE(signature, 0x030000);
  EXPECT_LE(signature, 0x090000);
  */
}

}  // namespace encoder

}  // namespace cobalt
