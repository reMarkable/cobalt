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

#include "analyzer/report_master/histogram_analysis_engine.h"

#include <memory>
#include <string>
#include <vector>

#include "./observation.pb.h"
#include "algorithms/forculus/forculus_analyzer.h"
#include "algorithms/rappor/basic_rappor_analyzer.h"
#include "glog/logging.h"

namespace cobalt {
namespace analyzer {

using config::AnalyzerConfig;
using forculus::ForculusAnalyzer;
using rappor::BasicRapporAnalyzer;
using store::ObservationStore;
using store::ReportStore;

namespace {
// Checks that the type of encoding used by the observation_part is the
// one specified by the encoding_config.
bool CheckConsistentEncoding(const EncodingConfig& encoding_config,
                             const ObservationPart& observation_part,
                             const ReportId& report_id) {
  bool consistent = true;
  switch (observation_part.value_case()) {
    case ObservationPart::kForculus:
      consistent = encoding_config.has_forculus();
      break;
    case ObservationPart::kBasicRappor:
      consistent = encoding_config.has_basic_rappor();
      break;
    case ObservationPart::kRappor:
      consistent = encoding_config.has_rappor();
      break;
    case ObservationPart::VALUE_NOT_SET:
      consistent = false;
      break;

    default:
      LOG(FATAL) << "Unexpected case " << observation_part.value_case();
  }
  if (!consistent) {
    LOG(ERROR) << "Bad ObservationPart! Value uses encoding "
               << observation_part.value_case() << " but "
               << encoding_config.config_case() << " expected."
               << " For report_id=" << ReportStore::ToString(report_id);
  }

  return consistent;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////
/// class ForculusAdapter
//
// A concrete subclass of DecoderAdapter that adapts to a ForculsAnalyzer.
///////////////////////////////////////////////////////////////////////////
class ForculusAdapter : public DecoderAdapter {
 public:
  ForculusAdapter(const ReportId& report_id,
                  const cobalt::ForculusConfig& config)
      : report_id_(report_id), analyzer_(new ForculusAnalyzer(config)) {}

  bool ProcessObservationPart(uint32_t day_index,
                              const ObservationPart& obs) override {
    return analyzer_->AddObservation(day_index, obs.forculus());
  }

  grpc::Status PerformAnalysis(std::vector<ReportRow>* results) override {
    auto result_map = analyzer_->TakeResults();
    results->clear();
    for (const auto& pair : result_map) {
      ValuePart value_part;
      if (!value_part.ParseFromString(pair.first)) {
        LOG(ERROR) << "Bad value. Could not parse as ValuePart: " << pair.first
                   << "report_id=" << ReportStore::ToString(report_id_);
        continue;
      }
      results->emplace_back();
      HistogramReportRow* row = results->back().mutable_histogram();
      row->mutable_value()->Swap(&value_part);
      row->set_count_estimate(pair.second->total_count);
      // TODO(rudominer) We are not using some of the data that the
      // ForculusAnalyzer can return to us such as observation_errors().
      // Consider adding monitoring around this.
    }

    return grpc::Status::OK;
  }

 private:
  ReportId report_id_;
  std::unique_ptr<ForculusAnalyzer> analyzer_;
};

////////////////////////////////////////////////////////////////////////////
/// class RapporAdapter
//
// A concrete subclass of DecoderAdapter that adapts to a
// StringRapporAnalyzer.
//
// NOTE: String RAPPOR analysis is not yet implemented in Cobalt.
///////////////////////////////////////////////////////////////////////////
class RapporAdapter : public DecoderAdapter {
 public:
  bool ProcessObservationPart(uint32_t day_index,
                              const ObservationPart& obs) override {
    return false;
  }

  grpc::Status PerformAnalysis(std::vector<ReportRow>* results) override {
    return grpc::Status(grpc::UNIMPLEMENTED,
                        "String RAPPOR analysis in not yet implemented.");
  }
};

////////////////////////////////////////////////////////////////////////////
/// class BasicRapporAdapter
//
// A concrete subclass of DecoderAdapter that adapts to a BasicRapporAnalyzer.
///////////////////////////////////////////////////////////////////////////
class BasicRapporAdapter : public DecoderAdapter {
 public:
  BasicRapporAdapter(const ReportId& report_id,
                     const cobalt::BasicRapporConfig& config)
      : report_id_(report_id), analyzer_(new BasicRapporAnalyzer(config)) {}

  bool ProcessObservationPart(uint32_t day_index,
                              const ObservationPart& obs) override {
    return analyzer_->AddObservation(obs.basic_rappor());
  }

  grpc::Status PerformAnalysis(std::vector<ReportRow>* results) override {
    auto category_results = analyzer_->Analyze();
    for (auto& category_result : category_results) {
      results->emplace_back();
      HistogramReportRow* row = results->back().mutable_histogram();
      row->mutable_value()->Swap(&category_result.category);
      row->set_count_estimate(category_result.count_estimate);
      row->set_std_error(category_result.std_error);
    }
    // TODO(rudominer) We are not using some of the data that the
    // BasicRapporAnalyzer can return to us such as observation_errors().
    // Consider adding monitoring around this.
    return grpc::Status::OK;
  }

 private:
  ReportId report_id_;
  std::unique_ptr<BasicRapporAnalyzer> analyzer_;
};

////////////////////////////////////////////////////////////////////////////
/// HistogramAnalysisEngine methods.
///////////////////////////////////////////////////////////////////////////
HistogramAnalysisEngine::HistogramAnalysisEngine(
    const ReportId& report_id, std::shared_ptr<AnalyzerConfig> analyzer_config)
    : report_id_(report_id), analyzer_config_(analyzer_config) {}

bool HistogramAnalysisEngine::ProcessObservationPart(
    uint32_t day_index, const ObservationPart& obs) {
  DecoderAdapter* decoder = GetDecoder(obs);
  if (!decoder) {
    return false;
  }
  return decoder->ProcessObservationPart(day_index, obs);
}

// Note that despite the comments in histogram_analysis_engine.h, version 0.1 of
// Cobalt does not yet support reports that are heterogeneous with respect
// to encoding. In this version the purpose of the HistogramAnalysisEngine is to
// ensure that in fact the set of observations is not heterogeneous.
grpc::Status HistogramAnalysisEngine::PerformAnalysis(
    std::vector<ReportRow>* results) {
  CHECK(results);

  if (decoders_.size() > 1) {
    std::ostringstream stream;
    stream << "Analysis aborted because more than one encoding_config_id was "
              "found among the observations: ";
    bool first = true;
    for (const auto& pair : decoders_) {
      if (!first) {
        stream << ", ";
      }
      stream << pair.first;
      first = false;
    }
    stream << ". This version of Cobalt does not "
              "support heterogeneous reports. report_id="
           << ReportStore::ToString(report_id_);
    std::string message = stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::UNIMPLEMENTED, message);
  }

  if (decoders_.size() == 0) {
    std::ostringstream stream;
    stream << "Analysis failed. No valid observations were added. report_id="
           << ReportStore::ToString(report_id_);
    std::string message = stream.str();
    LOG(ERROR) << message;
    return grpc::Status(grpc::FAILED_PRECONDITION, message);
  }

  return decoders_.begin()->second->PerformAnalysis(results);
}

DecoderAdapter* HistogramAnalysisEngine::GetDecoder(
    const ObservationPart& observation_part) {
  uint32_t encoding_config_id = observation_part.encoding_config_id();
  const EncodingConfig* encoding_config = analyzer_config_->EncodingConfig(
      report_id_.customer_id(), report_id_.project_id(), encoding_config_id);
  if (!encoding_config) {
    LOG(ERROR) << "Bad ObservationPart! Contains invalid encoding_config_id "
               << encoding_config_id
               << " for report_id=" << ReportStore::ToString(report_id_);
    return nullptr;
  }
  if (!CheckConsistentEncoding(*encoding_config, observation_part,
                               report_id_)) {
    return nullptr;
  }

  auto iter = decoders_.find(encoding_config_id);
  if (iter != decoders_.end()) {
    return iter->second.get();
  }
  // This is the first time we have seen the |encoding_config_id|. Make
  // a new decoder/analyzer for it.
  decoders_[encoding_config_id] = NewDecoder(encoding_config);

  return decoders_[encoding_config_id].get();
}

std::unique_ptr<DecoderAdapter> HistogramAnalysisEngine::NewDecoder(
    const EncodingConfig* encoding_config) {
  switch (encoding_config->config_case()) {
    case EncodingConfig::kForculus:
      return std::unique_ptr<DecoderAdapter>(
          new ForculusAdapter(report_id_, encoding_config->forculus()));
    case EncodingConfig::kRappor:
      return std::unique_ptr<DecoderAdapter>(new RapporAdapter);
    case EncodingConfig::kBasicRappor:
      return std::unique_ptr<DecoderAdapter>(
          new BasicRapporAdapter(report_id_, encoding_config->basic_rappor()));
    default:
      LOG(FATAL) << "Unexpected EncodingConfig type "
                 << encoding_config->config_case();
  }
}

}  // namespace analyzer
}  // namespace cobalt
