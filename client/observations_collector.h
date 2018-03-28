// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a library to be used by users of Cobalt in order to
// collect metrics at a high frequency. The main building blocks are the
// ObservationsCollector and Counter classes.
//
// Example: counting function calls
//
// ObservationsCollector collector(send_to_cobalt_function_pointer,
//                                 kDefaultEncodingId);
//
// auto foo_calls = collector.MakeCounter("foo_calls");
// auto foo_calls = collector.MakeCounter("bar_calls");
//
// // Perform aggregation and send to Cobalt FIDL service every 1 second.
// collector.Start(std::chrono::seconds(1));
//
// void Foo() {
//   foo_calls.Increment();
//   DoSomeFooWork
//   ...
// }
//
// void Bar() {
//   bar_calls.Increment();
//   DoSomeBarWork
//   ...
// }

#ifndef COBALT_CLIENT_OBSERVATIONS_COLLECTOR_H_
#define COBALT_CLIENT_OBSERVATIONS_COLLECTOR_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "client/observation.h"

namespace cobalt {
namespace client {

// An SendObservationsFn is a callable object that takes a pointer to a vector
// of observations and returns a list of the observation indices for
// observations that failed to be sent.
typedef std::function<std::vector<size_t>(std::vector<Observation>*)>
    SendObservationsFn;

// A Counter allows you to keep track of the number of times an event has
// occured. Every counter has an associated metric part.
// A Counter can be incremented from an arbitrary number of threads.
class Counter {
 public:
  // Increments the counter by 1.
  inline void Increment() { counter_++; }

 private:
  friend class MetricObservers;

  // Make a counter with the specified part name.
  static std::shared_ptr<Counter> Make(const std::string& part_name,
                                       uint32_t encoding_id) {
    return std::shared_ptr<Counter>(new Counter(part_name, encoding_id));
  }

  explicit Counter(const std::string& part_name, uint32_t encoding_id)
      : counter_(0), part_name_(part_name), encoding_id_(encoding_id) {}

  // Returns an integer ObservationPart and sets the counter's value to 0.
  // If the ObservationPart undo function is called, the counter's value is
  // added back on top of the counter.
  ObservationPart GetObservationPart();

  std::atomic<int64_t> counter_;
  std::string part_name_;
  uint32_t encoding_id_;
};

// A MetricObservers allows you to group together several observers that
// correspond to metric parts.
class MetricObservers {
 public:
  // Makes a Counter associated with this metric.
  // The part_name specified must correspond to an integer part name.
  // The encoding_id specified must be the id of an encoding in the cobalt
  // config.
  std::shared_ptr<Counter> MakeCounter(const std::string& part_name,
                                       uint32_t encoding_id);

 private:
  friend class ObservationsCollector;

  static std::shared_ptr<MetricObservers> Make(uint32_t id);

  explicit MetricObservers(uint32_t id) : id_(id) {}

  // Gets the Observation.
  Observation GetObservation();

  // MetricObservers id.
  uint32_t id_;
  // Map of counters part_name -> Counter.
  std::map<std::string, std::shared_ptr<Counter>> counters_;
};

// A ObservationsCollector tracks various metrics, collects their values into
// observations and sends them.
class ObservationsCollector {
 public:
  // send_observations will be used to send the collected observations.
  // default_encoding_id is the encoding id used unless another one is
  // specified.
  explicit ObservationsCollector(SendObservationsFn send_observations,
                                 uint32_t default_encoding_id)
      : send_observations_(send_observations),
        default_encoding_id_(default_encoding_id) {}

  // Makes a Counter object for the specified metric id, part name and to be
  // encoded using the default encoding id.
  std::shared_ptr<Counter> MakeCounter(uint32_t metric_id,
                                       const std::string& part_name);

  // Makes a Counter object for the specified metric id, part name and to be
  // encoded using the specified encoding id.
  std::shared_ptr<Counter> MakeCounter(uint32_t metric_id,
                                       const std::string& part_name,
                                       uint32_t encoding_id);

  // Starts a new thread that collects and attempts to send metrics every
  // |collection_interval|.
  // Calling Start more than once without first calling Stop has undefined
  // behavior.
  void Start(std::chrono::nanoseconds collection_interval);

  // Instructs the collection thread started by Start to stop and joins that
  // thread.
  void Stop();

  // CollectAll attempts to collect observations for all MetricObservers
  // created with this collector and send them using |send_observations|.
  void CollectAll();

 private:
  std::shared_ptr<MetricObservers> GetMetricObservers(uint32_t id);

  void CollectLoop(std::chrono::nanoseconds collection_interval);

  // Map of metric id -> MetricObservers.
  std::map<uint32_t, std::shared_ptr<MetricObservers>> metrics_;
  // Thread on which the collection loop is run.
  std::thread collection_loop_;
  // Set to false to stop collection.
  bool collection_loop_continue_;
  // Call this function to send observations.
  SendObservationsFn send_observations_;
  // The encoding id to be used when none is specified.
  uint32_t default_encoding_id_;
};

}  // namespace client
}  // namespace cobalt

#endif  // COBALT_CLIENT_OBSERVATIONS_COLLECTOR_H_
