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

#ifndef COBALT_ANALYZER_ENCODING_MIXER_H_
#define COBALT_ANALYZER_ENCODING_MIXER_H_

#include <map>
#include <memory>
#include <vector>

#include "./encrypted_message.pb.h"
#include "./observation.pb.h"
#include "algorithms/forculus/forculus_analyzer.h"
#include "analyzer/store/observation_store.h"
#include "analyzer/store/report_store.h"
#include "config/encoding_config.h"
#include "config/metric_config.h"
#include "config/report_config.h"
#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {

// Forward declaration.
class DecoderAdapter;

// An EncodingMixer is responsible for coordinating the analysis of a set of
// observations that are possibly heterogeneous with respect to their encodings.
// The observations are aggregated into homogeneous groups, the appropriate
// decoder/analyzer is applied to each group, and the analysis results are
// combined into a final result.
//
// An instance of EncodingMixer is used just once, for one single-variable
// report (or a single-variable slice of a two-variable report). It does not
// know how to deal with two-variable reports. An EncodingMixer is used by
// a ReportGenerator which knows how to deal with two-variable reports.
//
// usage:
//   - Construct an EncodingMixer.
//   - Invoke ProcessObservationPart() multiple times. The ObservationParts
//     passed in are allowed to have different encoding_config_ids from
//     each other, but they must all be for the same single-variable report
//     or variable-slice. (NOTE: Encoding-heterogeneous reports are not yet
//     supported in V0.1 of Cobalt. Currently all ObservationParts passed
//     in to ProcessObservationPart() must in fact have the same
//     encoding_config_id.)
//   - Invoke PerformAnalysis() to retrieve the results.
class EncodingMixer {
 public:
  // Constructs an EncodingMixer for the single-variable report with the
  // given |report_id|. The |encoding_configs| parameter is used to look up
  // EncodingConfigs by their ID.
  EncodingMixer(const ReportId& report_id,
                std::shared_ptr<config::EncodingRegistry> encoding_configs);

  // Process the given (day_index, ObservationPart) pair. The |day_index|
  // indicates the day on which the ObservationPart was observed, as specified
  // by the Encoder client. The |encoding_config_id| from the ObservationPart
  // will be looked up in the EncodingRegistry passed to the constructor and
  // this will determine which decooder/analyzer is used to process the
  // ObservationPart.
  //
  // Returns true if the ObservationPart was processed without error or false
  // otherwise.
  bool ProcessObservationPart(uint32_t day_index, const ObservationPart& obs);

  // Performs the appropriate analyses on the ObservationParts introduced
  // via ProcessObservationPart(). If the set of observations was heterogeneous
  // then multiple analyses are combined as appropriate. (Again, this is not
  // yet supported in V0.1 of Cobalt.) The results are written
  // into |results| and the returned Status indicates success or error.
  grpc::Status PerformAnalysis(std::vector<ReportRow>* results);

 private:
  // Returns the DecoderAdapter appropriate for decoding the given
  // |observation_part|.
  DecoderAdapter* GetDecoder(const ObservationPart& observation_part);

  // Constructs a new DecoderAdapter appropriate for the given
  // |encoding_config|.
  std::unique_ptr<DecoderAdapter> NewDecoder(
      const EncodingConfig* encoding_config);

  // The ID of the single-variable report (or report-slice) this EncodingMixer
  // is for.
  ReportId report_id_;

  // The keys to this map are encoding-config IDs and the values are the
  // DecoderAdapters adapting to the decoder/analyzer that knows how to
  // decode the correspodning encoding.
  std::map<uint32_t, std::unique_ptr<DecoderAdapter>> decoders_;

  // The registry of EncodingConfigs.
  std::shared_ptr<config::EncodingRegistry> encoding_configs_;
};

// A DecoderAdapter offers a common interface for the EncodingMixer to use
// while encapsulating heterogeneous backend interfaces to the underlying
// privacy-preserving algorithm decoder/analyzers.
//
// This is an abstract class. Concrete subclasses adapt to a particular
// algorithm.
class DecoderAdapter {
 public:
  virtual bool ProcessObservationPart(uint32_t day_index,
                                      const ObservationPart& obs) = 0;

  virtual grpc::Status PerformAnalysis(std::vector<ReportRow>* results) = 0;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_ENCODING_MIXER_H_
