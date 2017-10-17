// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_SYSTEM_DATA_H_
#define COBALT_ENCODER_SYSTEM_DATA_H_

#include "./observation.pb.h"

namespace cobalt {
namespace encoder {

// An abstraction of the interface to SystemData that allows mocking in
// tests.
class SystemDataInterface {
 public:
  // Returns the SystemProfile for the current system.
  virtual const SystemProfile& system_profile() const = 0;
};

// The Encoder client creates a singleton instance of SystemData at start-up
// time and uses it to query data about the client's running system. There
// are two categories of data: static data about the system encapsulated in
// the SystemProfile, and dynamic stateful data about the running system.
class SystemData : public SystemDataInterface {
 public:
  // Constructor: Populuates system_profile_ with the real SystemProfile
  // of the actual running system.
  SystemData();

  // Returns the SystemProfile for the current system.
  const SystemProfile& system_profile() const override {
    return system_profile_;
  }

 private:
  void PopulateSystemProfile();

  SystemProfile system_profile_;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_SYSTEM_DATA_H_
