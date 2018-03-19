// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a library to be used by users of Cobalt in order to
// collect metrics at a high frequency. The main building blocks are the
// MetricsCollector, Metric and Counter classes.
//
// Example: counting function calls
//
// MetricsCollector collector;
//
// auto call_tracker = collector.MakeMetric(10);
//
// auto foo_calls = call_tracker->MakeCounter("foo_calls");
// auto bar_calls = call_tracker->MakeCounter("bar_calls");
//
// // Collect data every 1 second.
// collector.StartCollecting(std::chrono::seconds(1));
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

#ifndef COBALT_CLIENT_METRICS_H_
#define COBALT_CLIENT_METRICS_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace cobalt {
namespace client {

// TODO(azani): Move a bunch of stuff into an internal namespace and maybe
// separate file.

// An UndoFunction is called to indicate a collection attempt has failed and
// must be undone.
typedef std::function<void()> UndoFunction;

// An ObservationPart represents a collected observation part. It currently
// only supports integers.
struct ObservationPart {
  ObservationPart(std::string part_name, int64_t value, UndoFunction undo)
      : part_name(part_name), value(value), undo(undo) {}

  std::string part_name;
  // TODO(azani): Generalize beyond integers.
  int64_t value;
  // Calling undo will undo the collection of the metric part.
  // TODO(azani): Maybe make private.
  UndoFunction undo;
  // TODO(azani): Add encoding information.
};

// An Observation represents a collected observation to be sent to Cobalt.
struct Observation {
  uint32_t metric_id;
  std::vector<ObservationPart> parts;
};

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
  friend class Metric;

  // Make a counter with the specified part name.
  static std::shared_ptr<Counter> Make(const std::string& part_name) {
    return std::shared_ptr<Counter>(new Counter(part_name));
  }

  explicit Counter(const std::string& part_name)
      : counter_(0), part_name_(part_name) {}

  // Returns an ObservationPart and sets the counter's value to 0.
  // If the ObservationPart undo function is called, the counter's value is
  // added back on top of the counter.
  ObservationPart GetObservationPart();

  // TODO(azani): Add encoding info.
  std::atomic<int64_t> counter_;
  std::string part_name_;
};

// A Metric allows you to group together several metric parts.
class Metric {
 public:
  // Makes a Counter associated with this metric.
  // The part_name specified must correspond to an integer part name.
  std::shared_ptr<Counter> MakeCounter(const std::string& part_name);

 private:
  friend class MetricsCollector;

  static std::shared_ptr<Metric> Make(uint32_t id);

  explicit Metric(uint32_t id) : id_(id) {}

  // Gets the Observation.
  Observation GetObservation();

  // Metric id.
  uint32_t id_;
  // List of counters.
  std::vector<std::shared_ptr<Counter>> counters_;
};

// A MetricsCollector tracks various metrics, collects their values into
// observations and sends them.
class MetricsCollector {
 public:
  // send_observations will be used to send the collected observations.
  explicit MetricsCollector(SendObservationsFn send_observations)
      : send_observations_(send_observations) {}

  // Equivalent to calling MakeMetric(id)->MakeCounter(part_name);
  std::shared_ptr<Counter> MakeCounter(int64_t metric_id,
                                         const std::string& part_name);

  // Makes a Metric associated with this MetricFactory. This metric will be
  // collected when collection occures.
  std::shared_ptr<Metric> MakeMetric(uint32_t id);

  // Starts a new thread that collects and attempts to send metrics every
  // |collection_interval|.
  // Calling StartCollecting more than once without first calling StopCollecting
  // has undefined behavior.
  void StartCollecting(std::chrono::nanoseconds collection_interval);

  // Instructs the collection thread started by StartCollecting to stop and
  // joins that thread.
  void StopCollecting();

  // CollectAll attempts to collect observations for all Metrics created with
  // this collector and send them using |send_observations|.
  void CollectAll();

 private:
  void CollectLoop(std::chrono::nanoseconds collection_interval);

  // List of Metric objects.
  std::vector<std::shared_ptr<Metric>> metrics_;
  // Thread on which the collection loop is run.
  std::thread collection_loop_;
  // Set to false to stop collection.
  bool collection_loop_continue_;
  // Call this function to send observations.
  SendObservationsFn send_observations_;
};

}  // namespace client
}  // namespace cobalt

#endif  // COBALT_CLIENT_METRICS_H_
