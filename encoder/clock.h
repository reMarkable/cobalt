// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ENCODER_CLOCK_H_
#define COBALT_ENCODER_CLOCK_H_

#include <chrono>

// Allows us to mock out a clock for tests.
class ClockInterface {
 public:
  virtual std::chrono::time_point<std::chrono::system_clock> now() = 0;
};

// A clock that returns the real system time.
class SystemClock : public ClockInterface {
 public:
  std::chrono::time_point<std::chrono::system_clock> now() override {
    return std::chrono::system_clock::now();
  }
};

// A clock that returns an incrementing sequence of tics each time it is called.
class IncrementingClock : public ClockInterface {
 public:
  std::chrono::time_point<std::chrono::system_clock> now() override {
    time_ += increment_;
    return time_;
  }

  // Return the current value of time_ without advancing time.
  std::chrono::time_point<std::chrono::system_clock> peek_now() {
    return time_;
  }

  void set_increment(std::chrono::system_clock::duration increment) {
    increment_ = increment;
  }

 private:
  std::chrono::system_clock::time_point time_ =
      std::chrono::system_clock::time_point(
          std::chrono::system_clock::duration(0));
  std::chrono::system_clock::duration increment_ =
      std::chrono::system_clock::duration(1);
};

#endif  // COBALT_ENCODER_CLOCK_H_
