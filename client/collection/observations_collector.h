// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a library to be used by users of Cobalt in order to
// collect metrics at a high frequency. The main building blocks are the
// ObservationsCollector, Counter and IntegerSampler classes.
//
// Example: counting and timing function calls
//
// ObservationsCollector collector(send_to_cobalt_function_pointer,
//                                 kDefaultEncodingId);
//
// auto foo_calls = collector.MakeCounter(kFooCallsMetricId,
//                                        kFooCallsMetricPartName);
//
// auto bar_calls = collector.MakeCounter(kBarCallsMetricId,
//                                        kBarCallsMetricPartName);
//
//
// auto foo_call_time_sampler = collector.MakeIntegerSampler(
//     kFooCallTimeMetricId, kFooCallTimeMetricPartName, kNumberOfSamples);
//
// // Perform aggregation and send to Cobalt FIDL service every 1 second.
// collector.Start(std::chrono::seconds(1));
//
// void Foo() {
//   int64_t start = getCurTime();
//   foo_calls.Increment();
//   DoSomeFooWork
//   ...
//   // Logs the amount of time Foo took to execute to the foo_call_sampler
//   // which will randomly select kNumberOfSamples observations to be sent to
//   // Cobalt.
//   foo_call_time_sampler.LogObservation(getCurTime() - start);
// }
//
// void Bar() {
//   bar_calls.Increment();
//   DoSomeBarWork
//   ...
// }

#ifndef COBALT_CLIENT_COLLECTION_OBSERVATIONS_COLLECTOR_H_
#define COBALT_CLIENT_COLLECTION_OBSERVATIONS_COLLECTOR_H_

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

#include "client/collection/observation.h"

namespace cobalt {
namespace client {

// A SendObservationsFn is a callable object that takes a pointer to a vector
// of observations and returns a list of the observation indices for
// observations that failed to be sent. An empty list is returned on success.
// The expectation is that this function will send observations to a consumer
// such as sending observations to the Cobalt FIDL service on Fuchsia.
typedef std::function<std::vector<size_t>(std::vector<Observation>*)>
    SendObservationsFn;

// A Counter allows you to keep track of the number of times an event has
// occured. A counter is associated with a metric part.
// Incrementing a counter is thread-safe.
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

// A Sampler has an associated |size| passed as |samples| to the Make*Sampler()
// method on the ObservationsCollector.
// Each collection period, the Sampler will attempt to uniformly sample up to
// |size| of the logged observations. The sampled observations will be
// collected by the ObservationsCollector.
// LogObservation is thread-safe.
template <class T>
class Sampler {
 public:
  void LogObservation(const T& value) {
    uint64_t idx = num_seen_++;
    // idx should now be a unique number.

    if (idx < size_) {
      reservoir_[idx] = value;
      num_written_++;
    }

    // TODO(azani): Handle the case where num_seen_ > size_.
  }

 private:
  friend class ObservationsCollector;

  static std::shared_ptr<Sampler<T>> Make(uint32_t metric_id,
                                          const std::string& part_name,
                                          uint32_t encoding_id,
                                          size_t samples) {
    return std::shared_ptr<Sampler<T>>(
        new Sampler(metric_id, part_name, encoding_id, samples));
  }

  Sampler(uint32_t metric_id, const std::string& part_name,
          uint32_t encoding_id, size_t samples)
      : metric_id_(metric_id),
        part_name_(part_name),
        encoding_id_(encoding_id),
        size_(samples),
        reservoir_(new std::atomic<T>[size_]),
        num_seen_(0),
        num_written_(0) {}

  ValuePart GetValuePart(size_t idx);

  void AppendObservations(std::vector<Observation>* observations) {
    for (size_t i = 0; i < num_written_; i++) {
      Observation observation;
      observation.metric_id = metric_id_;
      // TODO(azani): Figure out how to do the undo function.
      observation.parts.push_back(
          ObservationPart(part_name_, encoding_id_, GetValuePart(i), []() {}));
      observations->push_back(observation);
    }
    num_written_ = 0;
    num_seen_ = 0;
  }

  uint32_t metric_id_;
  std::string part_name_;
  uint32_t encoding_id_;
  // Reservoir size.
  size_t size_;
  std::unique_ptr<std::atomic<T>[]> reservoir_;
  std::atomic<size_t> num_seen_;
  // num_written_ is used to determin how many values are available to be read.
  std::atomic<size_t> num_written_;
};

using IntegerSampler = Sampler<int64_t>;

// A MetricObservers allows you to group together several observers that
// correspond to metric parts.
class MetricObservers {
 public:
  // Makes a Counter associated with this metric.
  // The part_name specified must be the name of an integer part.
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
  // default_encoding_id is the encoding id used when no other encoding id
  // is used while making Counters or Samplers.
  explicit ObservationsCollector(SendObservationsFn send_observations,
                                 uint32_t default_encoding_id)
      : send_observations_(send_observations),
        default_encoding_id_(default_encoding_id) {}

  // Makes a Counter object for the specified metric id, part name and
  // encoded using the default encoding id.
  std::shared_ptr<Counter> MakeCounter(uint32_t metric_id,
                                       const std::string& part_name);

  // Makes a Counter object for the specified metric id, part name and
  // encoded using the specified encoding id.
  std::shared_ptr<Counter> MakeCounter(uint32_t metric_id,
                                       const std::string& part_name,
                                       uint32_t encoding_id);

  // Makes an IntegerSampler for the specified metric id, part name and
  // encoded using the specified encoding id. At most, |samples| samples will be
  // collected per collection period.
  std::shared_ptr<IntegerSampler> MakeIntegerSampler(
      uint32_t metric_id, const std::string& part_name, uint32_t encoding_id,
      size_t samples);

  // Makes an IntegerSampler for the specified metric id, part name and
  // encoded using the default encoding id. At most, |samples| samples will be
  // collected per collection period.
  std::shared_ptr<IntegerSampler> MakeIntegerSampler(
      uint32_t metric_id, const std::string& part_name, size_t samples);

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
  std::vector<std::function<void(std::vector<Observation>*)>>
      reservoir_samplers_;
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

#endif  // COBALT_CLIENT_COLLECTION_OBSERVATIONS_COLLECTOR_H_
