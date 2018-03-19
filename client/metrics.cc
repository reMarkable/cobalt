// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client/metrics.h"

namespace cobalt {
namespace client {

ObservationPart Counter::GetObservationPart() {
  // Atomically swaps the value in counter_ for 0 and puts the former value of
  // counter_ in value.
  int64_t value = counter_.exchange(0);
  // If the undo function is called, it adds |value| back to the counter_.
  return ObservationPart(part_name_, value,
                         [this, value]() { counter_ += value; });
}

std::shared_ptr<Metric> Metric::Make(uint32_t id) {
  return std::shared_ptr<Metric>(new Metric(id));
}

std::shared_ptr<Counter> Metric::MakeCounter(const std::string& part_name) {
  auto counter = Counter::Make(part_name);
  counters_.push_back(counter);
  return counter;
}

Observation Metric::GetObservation() {
  Observation observation;
  observation.metric_id = id_;

  for (auto iter = counters_.begin(); iter != counters_.end(); iter++) {
    observation.parts.push_back((*iter)->GetObservationPart());
  }
  return observation;
}

std::shared_ptr<Counter> MetricsCollector::MakeCounter(
    int64_t metric_id, const std::string& part_name) {
  return MakeMetric(metric_id)->MakeCounter(part_name);
}

std::shared_ptr<Metric> MetricsCollector::MakeMetric(uint32_t id) {
  auto metric = Metric::Make(id);
  metrics_.push_back(metric);
  return metric;
}

void MetricsCollector::StartCollecting(
    std::chrono::nanoseconds collection_interval) {
  collection_loop_continue_ = true;
  collection_loop_ =
      std::thread(&MetricsCollector::CollectLoop, this, collection_interval);
}

void MetricsCollector::StopCollecting() {
  collection_loop_continue_ = false;
  collection_loop_.join();
}

void MetricsCollector::CollectAll() {
  std::vector<Observation> observations;
  for (auto iter = metrics_.begin(); iter != metrics_.end(); iter++) {
    observations.push_back((*iter)->GetObservation());
  }
  auto errors = send_observations_(&observations);

  // Undo failed observations.
  for (auto iter = errors.begin(); iter != errors.end(); iter++) {
    for (auto parts_iter = observations[*iter].parts.begin();
         parts_iter != observations[*iter].parts.end(); parts_iter++) {
      parts_iter->undo();
    }
  }
}

void MetricsCollector::CollectLoop(
    std::chrono::nanoseconds collection_interval) {
  while (collection_loop_continue_) {
    CollectAll();
    // TODO(azani): Add jitter.
    std::this_thread::sleep_for(collection_interval);
  }
}
}  // namespace client
}  // namespace cobalt
