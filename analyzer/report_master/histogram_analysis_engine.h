// Copyright 2017 The Fuchsia Authors
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

#ifndef COBALT_ANALYZER_REPORT_MASTER_HISTOGRAM_ANALYSIS_ENGINE_H_
#define COBALT_ANALYZER_REPORT_MASTER_HISTOGRAM_ANALYSIS_ENGINE_H_

#include <map>
#include <memory>
#include <vector>

#include "./encrypted_message.pb.h"
#include "./observation.pb.h"
#include "algorithms/forculus/forculus_analyzer.h"
#include "analyzer/report_master/report_generator.h"
#include "analyzer/store/observation_store.h"
#include "analyzer/store/report_store.h"
#include "config/analyzer_config.h"
#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {

// Forward declaration.
class DecoderAdapter;

// A HistogramAnalysisEngine is responsible for performing the analysis that
// leads to the generation of a Histogram report.
//
// The set of observations analyzed are allowed to be heterogeneous with respect
// to their encoding. The observations are aggregated into homogeneous groups,
// the appropriate decoder/analyzer is applied to each group, and the analysis
// results are combined into a final Histogram report.
//
// An instance of HistogramAnalysisEngine is used just once, for one Histogram
// report.
//
// usage:
//   - Construct a HistogramAnalysisEngine.
//   - Invoke ProcessObservationPart() multiple times.
//   - Invoke PerformAnalysis() to retrieve the rows of the Histogram report.
class HistogramAnalysisEngine {
 public:
  // Constructs a HistogramAnalysisEngine for the Histogram report with the
  // given |report_id|.
  //
  // The |report_variable| is used to look up any per-encoding
  // report configuration that may have been specified. Examples of this
  // are the String RAPPOR candidate list, and the category labels for
  // basic RAPPOR configured with indexed categories.
  //
  // The |analyzer_config| is used to look up EncodingConfigs by their ID.
  HistogramAnalysisEngine(
      const ReportId& report_id, const ReportVariable* report_variable,
      const MetricPart* metric_part,
      std::shared_ptr<config::AnalyzerConfig> analyzer_config);

  // Process the given (day_index, ObservationPart, SystemProfile) triple. The
  // |day_index| indicates the day on which the ObservationPart was observed, as
  // specified by the Encoder client. The |encoding_config_id| from the
  // ObservationPart will be looked up in the AnalyzerConfig passed to the
  // constructor and this will determine which decooder/analyzer is used to
  // process the ObservationPart. The SystemProfile describes the client system
  // on which the ObservationPart was observed. We group the ObservationParts by
  // the SystemProfile and perform a separate analysis for each group.
  //
  // Returns true if the ObservationPart was processed without error or false
  // otherwise.
  bool ProcessObservationPart(uint32_t day_index, const ObservationPart& obs,
                              std::unique_ptr<SystemProfile> profile);

  // Performs the appropriate analyses on the ObservationParts introduced
  // via ProcessObservationPart(). If the set of observations was heterogeneous
  // then multiple analyses are combined as appropriate. (This is not
  // yet supported in V0.1 of Cobalt.) The results are written
  // into |results| and the returned Status indicates success or error.
  grpc::Status PerformAnalysis(std::vector<ReportRow>* results);

 private:
  // Returns the DecoderAdapter appropriate for decoding the given
  // |observation_part|.
  DecoderAdapter* GetDecoder(const ObservationPart& observation_part,
                             std::unique_ptr<SystemProfile> profile);

  // Constructs a new DecoderAdapter appropriate for the given
  // |encoding_config|.
  std::unique_ptr<DecoderAdapter> NewDecoder(
      const EncodingConfig* encoding_config);

  // The ID of the Histogram report this HistogramAnalysisEngine is for.
  ReportId report_id_;

  // The variable being analyzed.
  const ReportVariable* report_variable_;

  // Pointer to the metric part for the variable being analyzed.
  const MetricPart* metric_part_;

  // Stores the shared SystemProfile for all decoders.
  struct DecoderGroup {
    // Used to group the decoders together.
    std::unique_ptr<SystemProfile> profile;

    // The keys to this map are encoding-config IDs and the values are the
    // DecoderAdapters adapting to the decoder/analyzer that knows how to
    // decode the correspodning encoding.
    std::map<uint32_t, std::unique_ptr<DecoderAdapter>> decoders;
  };

  // The keys to this map are string-encoded SystemProfiles.
  std::map<std::string, DecoderGroup> grouped_decoders_;

  // Contains the registry of EncodingConfigs.
  std::shared_ptr<config::AnalyzerConfig> analyzer_config_;
};

// A DecoderAdapter offers a common interface for the HistogramAnalysisEngine to
// use while encapsulating heterogeneous backend interfaces to the underlying
// privacy-preserving algorithm decoder/analyzers.
//
// This is an abstract class. Concrete subclasses adapt to a particular
// algorithm.
class DecoderAdapter {
 public:
  virtual ~DecoderAdapter() = default;

  virtual bool ProcessObservationPart(uint32_t day_index,
                                      const ObservationPart& obs) = 0;

  virtual grpc::Status PerformAnalysis(std::vector<ReportRow>* results) = 0;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_HISTOGRAM_ANALYSIS_ENGINE_H_
