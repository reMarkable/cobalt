// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client/observations_collector.h"

#include "gflags/gflags.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

const int64_t kPeriodSize = 1000;
const int64_t kPeriodCount = 1000;
const int64_t kThreadNum = 100;

namespace cobalt {
namespace client {

namespace {
// Function that increments a counter kPeriodSize * kPeriodCount times in
// kPeriodCount increments with some random jitter in between.
void DoIncrement(std::shared_ptr<Counter> counter) {
  for (int64_t i = 0; i < kPeriodCount; i++) {
    for (int64_t j = 0; j < kPeriodSize; j++) {
      counter->Increment();
    }
    // Introduce jitter to test.
    std::this_thread::sleep_for(std::chrono::microseconds(std::rand() % 100));
  }
}

// Sink is used to gather all the observations sent by the MetricFactory.
struct Sink {
  std::vector<size_t> SendObservations(std::vector<Observation>* obs) {
    std::vector<size_t> errors;

    for (auto iter = std::make_move_iterator(obs->begin());
         std::make_move_iterator(obs->end()) != iter; iter++) {
      // Randomly fail to "send" some observations.
      if (std::rand() % 5 == 0) {
        errors.push_back(
            std::distance(std::make_move_iterator(obs->begin()), iter));
        continue;
      }
      observations.push_back(*iter);
    }
    return errors;
  }

  std::vector<Observation> observations;
};

}  // namespace

// Checks that Counters work correctly with many threads updating them.
TEST(Counter, Normal) {
  // Metric id.
  const int64_t id = 10;
  Sink sink;
  ObservationsCollector collector(
      std::bind(&Sink::SendObservations, &sink, std::placeholders::_1), 1);
  auto counter = collector.MakeCounter(id, "part_name");

  // Each thread will add kPeriodSize * kPeriodCount to the counter.
  int64_t expected = kPeriodSize * kPeriodCount * kThreadNum;
  std::vector<std::thread> threads;

  // Start all the incrementer threads.
  for (int i = 0; i < kThreadNum; i++) {
    threads.push_back(std::thread(DoIncrement, counter));
  }

  // Start the collection thread.
  collector.Start(std::chrono::microseconds(10));

  // Wait until all the incrementer threads have finished.
  for (auto iter = threads.begin(); iter != threads.end(); iter++) {
    iter->join();
  }
  // Wait just a bit more than one collection period after the last incrementer
  // thread is done in order to ensure all the data is collected before we stop
  // collection.
  std::this_thread::sleep_for(std::chrono::microseconds(11));

  // Stop the collection thread.
  collector.Stop();

  // Add up all the observations in the sink.
  int64_t actual = 0;
  for (auto iter = sink.observations.begin(); sink.observations.end() != iter;
       iter++) {
    actual += (*iter).parts[0].value.GetIntValue();
  }

  EXPECT_EQ(expected, actual);
}

// Check that the integer value part work correctly.
TEST(ValuePart, IntValuePart) {
  ValuePart value = ValuePart::MakeIntValuePart(10);
  EXPECT_EQ(10, value.GetIntValue());
  EXPECT_TRUE(value.IsIntValue());
  EXPECT_EQ(ValuePart::INT, value.Which());
}
}  // namespace client
}  // namespace cobalt
