// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client/collection/observations_collector.h"

namespace cobalt {
namespace client {

ObservationPart Counter::GetObservationPart() {
  // Atomically swaps the value in counter_ for 0 and puts the former value of
  // counter_ in value.
  ValuePart value = ValuePart::MakeIntValuePart(counter_.exchange(0));
  // If the undo function is called, it adds |value| back to the counter_.
  return ObservationPart(part_name_, encoding_id_, value,
                         [this, value]() { counter_ += value.GetIntValue(); });
}

std::shared_ptr<MetricObservers> MetricObservers::Make(uint32_t id) {
  // An empty string for the collection period part name disables the collection
  // timer.
  return std::shared_ptr<MetricObservers>(new MetricObservers(id));
}

std::shared_ptr<Counter> MetricObservers::MakeCounter(
    const std::string& part_name, uint32_t encoding_id) {
  if (counters_.count(part_name) != 0) {
    return nullptr;
  }
  auto counter = Counter::Make(part_name, encoding_id);
  counters_[part_name] = counter;
  return counter;
}

Observation MetricObservers::GetObservation() {
  Observation observation;
  observation.metric_id = id_;

  for (auto iter = counters_.begin(); iter != counters_.end(); iter++) {
    observation.parts.push_back(iter->second->GetObservationPart());
  }

  return observation;
}

template <>
ValuePart Sampler<int64_t>::GetValuePart(size_t idx) {
  return ValuePart::MakeIntValuePart(reservoir_[idx]);
}

std::shared_ptr<Counter> ObservationsCollector::MakeCounter(
    uint32_t metric_id, const std::string& part_name, uint32_t encoding_id) {
  return GetMetricObservers(metric_id)->MakeCounter(part_name, encoding_id);
}

std::shared_ptr<Counter> ObservationsCollector::MakeCounter(
    uint32_t metric_id, const std::string& part_name) {
  return MakeCounter(metric_id, part_name, default_encoding_id_);
}

std::shared_ptr<IntegerSampler> ObservationsCollector::MakeIntegerSampler(
    uint32_t metric_id, const std::string& part_name, uint32_t encoding_id,
    size_t samples) {
  auto reservoir_sampler =
      Sampler<int64_t>::Make(metric_id, part_name, encoding_id, samples);
  reservoir_samplers_.push_back(
      [reservoir_sampler](std::vector<Observation>* observations) {
        reservoir_sampler->AppendObservations(observations);
      });
  return reservoir_sampler;
}

std::shared_ptr<IntegerSampler> ObservationsCollector::MakeIntegerSampler(
    uint32_t metric_id, const std::string& part_name, size_t samples) {
  return MakeIntegerSampler(metric_id, part_name, default_encoding_id_,
                            samples);
}

std::shared_ptr<MetricObservers> ObservationsCollector::GetMetricObservers(
    uint32_t metric_id) {
  if (metrics_.count(metric_id) == 0) {
    metrics_[metric_id] = MetricObservers::Make(metric_id);
  }
  return metrics_[metric_id];
}

void ObservationsCollector::Start(
    std::chrono::nanoseconds collection_interval) {
  collection_loop_continue_ = true;
  collection_loop_ = std::thread(&ObservationsCollector::CollectLoop, this,
                                 collection_interval);
}

void ObservationsCollector::Stop() {
  collection_loop_continue_ = false;
  collection_loop_.join();
}

void ObservationsCollector::CollectAll() {
  std::vector<Observation> observations;
  for (auto iter = metrics_.begin(); iter != metrics_.end(); iter++) {
    observations.push_back(iter->second->GetObservation());
  }

  for (auto iter = reservoir_samplers_.begin();
       iter != reservoir_samplers_.end(); iter++) {
    (*iter)(&observations);
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

void ObservationsCollector::CollectLoop(
    std::chrono::nanoseconds collection_interval) {
  while (collection_loop_continue_) {
    CollectAll();
    // TODO(azani): Add jitter.
    std::this_thread::sleep_for(collection_interval);
  }
}
}  // namespace client
}  // namespace cobalt
