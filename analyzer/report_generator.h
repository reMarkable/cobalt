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

#ifndef COBALT_ANALYZER_REPORT_GENERATOR_H_
#define COBALT_ANALYZER_REPORT_GENERATOR_H_

#include <map>
#include <memory>

#include "./encrypted_message.pb.h"
#include "./observation.pb.h"
#include "algorithms/forculus/forculus_analyzer.h"
#include "analyzer/store/observation_store.h"
#include "config/encoding_config.h"
#include "config/metric_config.h"
#include "config/report_config.h"

namespace cobalt {
namespace analyzer {

class ReportGenerator {
 public:
  explicit ReportGenerator(
      std::shared_ptr<config::MetricRegistry> metrics,
      std::shared_ptr<config::ReportRegistry> reports,
      std::shared_ptr<config::EncodingRegistry> encodings,
      std::shared_ptr<store::ObservationStore> observation_store);

  void GenerateReport(const ReportConfig& config);

 private:
  void ProcessObservation(const ReportConfig& config,
                          const ObservationMetadata& metadata,
                          const Observation& observation);

  std::shared_ptr<config::MetricRegistry> metrics_;
  std::shared_ptr<config::ReportRegistry> reports_;
  std::shared_ptr<config::EncodingRegistry> encodings_;
  std::shared_ptr<store::ObservationStore> observation_store_;

  // Reports are run serially per (customer, project, metric) triple.  Each
  // observation though can be encoded using different encodings.  We keep track
  // of all these encodings here.
  // key: id of encoding ; value: analyzer.
  // For now, only forculus is supported.
  std::map<uint32_t, std::unique_ptr<forculus::ForculusAnalyzer>> analyzers_;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_GENERATOR_H_
