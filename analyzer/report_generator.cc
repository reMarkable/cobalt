// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "analyzer/report_generator.h"

#include <glog/logging.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

using cobalt::config::EncodingRegistry;
using cobalt::config::MetricRegistry;
using cobalt::config::ReportRegistry;
using cobalt::forculus::ForculusAnalyzer;

namespace cobalt {
namespace analyzer {

using store::ObservationStore;

ReportGenerator::ReportGenerator(
    std::shared_ptr<MetricRegistry> metrics,
    std::shared_ptr<ReportRegistry> reports,
    std::shared_ptr<EncodingRegistry> encodings,
    std::shared_ptr<ObservationStore> observation_store)
    : metrics_(metrics),
      reports_(reports),
      encodings_(encodings),
      observation_store_(observation_store) {}

void ReportGenerator::GenerateReport(const ReportConfig& config) {
  LOG(INFO) << "Running report " << config.name();

  // As we process observations, we store results in analyzers_
  analyzers_.clear();

  // TODO(rudominer) Compute the real start and end day indices based on the
  // |aggregation_epoch_type|. For now we use [0, infinity)
  uint32_t start_day_index = 0;
  uint32_t end_day_index = UINT32_MAX;
  // TODO(rudominer) Build the parts vector from the |variable| field.
  // For now we use an empty parts vector indicating we want all parts.
  std::vector<std::string> parts;
  size_t max_results = 1000;
  ObservationMetadata metadata;
  metadata.set_customer_id(config.customer_id());
  metadata.set_project_id(config.project_id());
  metadata.set_metric_id(config.metric_id());

  store::ObservationStore::QueryResponse query_response;
  query_response.pagination_token = "";
  do {
    query_response = observation_store_->QueryObservations(
        config.customer_id(), config.project_id(), config.metric_id(),
        start_day_index, end_day_index, parts, max_results,
        query_response.pagination_token);

    if (query_response.status != store::kOK) {
      LOG(ERROR) << "QueryObservations() error: " << query_response.status;
      return;
    }

    LOG(INFO) << "Observations found: " << query_response.results.size();

    for (const auto& query_result : query_response.results) {
      metadata.set_day_index(query_result.day_index);
      // Process the observation.  This will populate analyzers_.
      ProcessObservation(config, metadata, query_result.observation);
    }
  } while (!query_response.pagination_token.empty());

  // See what results are available.
  for (auto& analyzer : analyzers_) {
    ForculusAnalyzer& forculus = *analyzer.second;
    auto results = forculus.TakeResults();

    for (auto& result : results) {
      LOG(INFO) << "Found plain-text:" << result.first;
    }
  }
}

void ReportGenerator::ProcessObservation(const ReportConfig& config,
                                         const ObservationMetadata& metadata,
                                         const Observation& observation) {
  // Figure out which metric we're dealing with.
  const Metric* const metric = metrics_->Get(
      config.customer_id(), config.project_id(), metadata.metric_id());

  if (!metric) {
    LOG(ERROR) << "Can't find metric ID " << metadata.metric_id()
               << " for customer " << config.customer_id() << " project "
               << config.project_id();
    return;
  }

  // Process all the parts.
  for (const auto& i : observation.parts()) {
    const std::string& name = i.first;
    const ObservationPart& part = i.second;

    // Check that the part name is expected.
    if (metric->parts().find(name) == metric->parts().end()) {
      LOG(ERROR) << "Unknown part name: " << name;
      continue;
    }

    // Figure out how the part is encoded.
    uint32_t eid = part.encoding_config_id();
    const EncodingConfig* const enc =
        encodings_->Get(config.customer_id(), config.project_id(), eid);

    if (!enc) {
      LOG(ERROR) << "Unknown encoding: " << eid;
      continue;
    }

    // XXX only support forculus for now.
    if (!enc->has_forculus()) {
      LOG(ERROR) << "Unsupported encoding: " << eid;
      continue;
    }

    // Grab the decoder.
    ForculusAnalyzer* forculus;
    auto iter = analyzers_.find(eid);

    if (iter != analyzers_.end()) {
      forculus = iter->second.get();
    } else {
      ForculusConfig forculus_conf;

      forculus_conf.set_threshold(enc->forculus().threshold());

      forculus = new ForculusAnalyzer(forculus_conf);
      analyzers_[eid] = std::unique_ptr<ForculusAnalyzer>(forculus);
    }

    if (!forculus->AddObservation(metadata.day_index(), part.forculus())) {
      LOG(ERROR) << "Can't add observation";
      continue;
    }
  }

  if (observation.parts().size() != metric->parts().size()) {
    VLOG(1) << "Not all parts present in observation";
  }
}

}  // namespace analyzer
}  // namespace cobalt
