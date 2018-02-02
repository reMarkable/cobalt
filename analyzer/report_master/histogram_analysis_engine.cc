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
#include "algorithms/rappor/rappor_analyzer.h"
#include "config/buckets_config.h"
#include "glog/logging.h"
#include "util/log_based_metrics.h"

namespace cobalt {
namespace analyzer {

using config::AnalyzerConfig;
using forculus::ForculusAnalyzer;
using rappor::BasicRapporAnalyzer;
using rappor::RapporAnalyzer;
using store::ObservationStore;
using store::ReportStore;

// Stackdriver metric constants
namespace {
const char kCheckConsistentEncodingFailure[] =
    "check-consistent-encoding-failure";
const char kForculusAdapterPerformAnalysisFailure[] =
    "forculus-adapter-perform-analysis-failure";
const char kRapporAdapterPerformAnalysisFailure[] =
    "rappor-adapter-perform-analysis-failure";
const char kNoOpAdapterProcessObservationPartFailure[] =
    "no-op-adapter-process-observation-part-failure";
const char kNoOpIntBucketDistributionAdapterProcessObservationPartFailure[] =
    "no-op-int-bucket-distribution-adapter-process-observation-part-failure";
const char kPerformAnalysisFailure[] =
    "histogram-analysis-engine-perform-analysis-failure";
const char kGetDecoderFailure[] =
    "histogram-analysis-engine-get-decoder-failure";
const char kNewDecoderFailure[] =
    "histogram-analysis-engine-new-decoder-failure";
}  // namespace

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
    case ObservationPart::kUnencoded:
      consistent = encoding_config.has_no_op_encoding();
      break;
    case ObservationPart::VALUE_NOT_SET:
      consistent = false;
      break;

    default:
      LOG(FATAL) << "Unexpected case " << observation_part.value_case();
  }
  if (!consistent) {
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kCheckConsistentEncodingFailure)
        << "Bad ObservationPart! Value uses encoding "
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
        LOG_STACKDRIVER_COUNT_METRIC(ERROR,
                                     kForculusAdapterPerformAnalysisFailure)
            << "Bad value. Could not parse as ValuePart: " << pair.first
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
// NOTE: String RAPPOR analysis is not yet fully implemented in Cobalt.
///////////////////////////////////////////////////////////////////////////
class RapporAdapter : public DecoderAdapter {
 public:
  RapporAdapter(const ReportId& report_id, const RapporConfig& config,
                const RapporCandidateList* candidates)
      : report_id_(report_id),
        analyzer_(new RapporAnalyzer(config, candidates)),
        candidates_(candidates) {}

  bool ProcessObservationPart(uint32_t day_index,
                              const ObservationPart& obs) override {
    return analyzer_->AddObservation(obs.rappor());
  }

  grpc::Status PerformAnalysis(std::vector<ReportRow>* results) override {
    std::vector<rappor::CandidateResult> candidate_results;
    auto status = analyzer_->Analyze(&candidate_results);
    if (!status.ok()) {
      LOG_STACKDRIVER_COUNT_METRIC(ERROR, kRapporAdapterPerformAnalysisFailure)
          << "String RAPPOR analysis failed with status=("
          << status.error_code() << ") " << status.error_message()
          << " For report_id=" << ReportStore::ToString(report_id_);
      return status;
    }
    // If candidates_ is null or empty then analyzer_->Analyze() will return
    // INVALID_ARGUMENT. If we are here then RAPPR analysis succeeded.
    CHECK((int)candidate_results.size() == candidates_->candidates_size());
    size_t candidate_index = 0;
    for (auto& candidate_result : candidate_results) {
      results->emplace_back();
      HistogramReportRow* row = results->back().mutable_histogram();
      ValuePart v;
      v.set_string_value(candidates_->candidates(candidate_index++));
      row->mutable_value()->Swap(&v);
      row->set_count_estimate(candidate_result.count_estimate);
      row->set_std_error(candidate_result.std_error);
    }
    // TODO(rudominer) We are not using some of the data that the
    // RapporAnalyzer can return to us such as observation_errors().
    // Consider adding monitoring around this.
    return grpc::Status::OK;
  }

 private:
  ReportId report_id_;
  std::unique_ptr<RapporAnalyzer> analyzer_;
  const RapporCandidateList* candidates_;  // not owned.
};

////////////////////////////////////////////////////////////////////////////
/// class BasicRapporAdapter
//
// A concrete subclass of DecoderAdapter that adapts to a BasicRapporAnalyzer.
///////////////////////////////////////////////////////////////////////////
class BasicRapporAdapter : public DecoderAdapter {
 public:
  BasicRapporAdapter(const ReportId& report_id,
                     const cobalt::BasicRapporConfig& config,
                     const IndexLabels* index_labels)
      : report_id_(report_id),
        analyzer_(new BasicRapporAnalyzer(config)),
        index_labels_(index_labels) {}

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
      // If the value is of type INDEX, meaning that it represents an index
      // into some enumerated set defined outside of the Cobalt configuration,
      // then check whether we were given an index label for this index and
      // if so attach the label to the report row.
      if (index_labels_ != nullptr &&
          row->value().data_case() == ValuePart::kIndexValue) {
        auto iter = index_labels_->labels().find(row->value().index_value());
        if (iter != index_labels_->labels().end()) {
          row->set_label(iter->second);
        }
      }
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
  const IndexLabels* index_labels_;  // not owned.
};

////////////////////////////////////////////////////////////////////////////
/// class NoOpAdapter
//
// A concrete subclass of DecoderAdapter that collects counts of
// UnencodedObservations in a hash map.
///////////////////////////////////////////////////////////////////////////
class NoOpAdapter : public DecoderAdapter {
 public:
  NoOpAdapter(const ReportId& report_id,
              const cobalt::NoOpEncodingConfig& config,
              const IndexLabels* index_labels)
      : report_id_(report_id), config_(config), index_labels_(index_labels) {}

  bool ProcessObservationPart(uint32_t day_index,
                              const ObservationPart& obs) override {
    std::string serialized_value;
    if (!obs.unencoded().unencoded_value().SerializeToString(
            &serialized_value)) {
      return false;
    }
    if (VLOG_IS_ON(5)) {
      std::ostringstream str;
      const auto& value = obs.unencoded().unencoded_value();
      switch (value.data_case()) {
        case ValuePart::kStringValue:
          str << value.string_value();
          break;
        case ValuePart::kIntValue:
          str << value.int_value();
          break;
        case ValuePart::kIndexValue:
          str << "index=" << value.index_value();
          break;
        case ValuePart::kDoubleValue:
          str << value.double_value();
        default:
          str << "[UNKNOWN DATA TYPE]";
      }
      VLOG(5) << "NoOpAdapter::ProcessObservationPart: " << str.str();
    }
    // For safety we will accept only up to 10,000 different values.
    static const size_t kMaxNumValues = 10000;
    if (counts_.size() >= kMaxNumValues) {
      LOG_STACKDRIVER_COUNT_METRIC(ERROR,
                                   kNoOpAdapterProcessObservationPartFailure)
          << "Report truncated! May not exceed " << kMaxNumValues
          << " different values."
          << " report_id=" << ReportStore::ToString(report_id_);
      return false;
    }
    counts_[serialized_value]++;
    return true;
  }

  grpc::Status PerformAnalysis(std::vector<ReportRow>* results) override {
    for (const auto& pair : counts_) {
      results->emplace_back();
      HistogramReportRow* row = results->back().mutable_histogram();
      row->mutable_value()->ParseFromString(pair.first);
      row->set_count_estimate(pair.second);
      row->set_std_error(0);
      // If the value is of type INDEX, meaning that it represents an index
      // into some enumerated set defined outside of the Cobalt configuration,
      // then check whether we were given an index label for this index and
      // if so attach the label to the report row.
      if (index_labels_ != nullptr &&
          row->value().data_case() == ValuePart::kIndexValue) {
        auto iter = index_labels_->labels().find(row->value().index_value());
        if (iter != index_labels_->labels().end()) {
          row->set_label(iter->second);
        }
      }
    }
    return grpc::Status::OK;
  }

 private:
  ReportId report_id_;
  cobalt::NoOpEncodingConfig config_;
  std::map<std::string, size_t> counts_;
  const IndexLabels* index_labels_;  // not owned.
};

////////////////////////////////////////////////////////////////////////////
/// class NoOpIntBucketDistributionAdapter
//
// A concrete subclass of DecoderAdapter that collects counts of bucketed
// integer observations and merges int bucket distribution observations.
///////////////////////////////////////////////////////////////////////////
class NoOpIntBucketDistributionAdapter : public DecoderAdapter {
 public:
  NoOpIntBucketDistributionAdapter(
      const ReportId& report_id, const cobalt::NoOpEncodingConfig& config,
      std::unique_ptr<cobalt::config::IntegerBucketConfig> int_bucket_config)
      : report_id_(report_id),
        config_(config),
        int_bucket_config_(std::move(int_bucket_config)) {}

  bool ProcessObservationPart(uint32_t day_index,
                              const ObservationPart& obs) override {
    if (obs.value_case() != ObservationPart::kUnencoded) {
      LOG_STACKDRIVER_COUNT_METRIC(
          ERROR, kNoOpIntBucketDistributionAdapterProcessObservationPartFailure)
          << "Encoded observation ignored. report_id="
          << ReportStore::ToString(report_id_);
      return false;
    }

    const auto& value = obs.unencoded().unencoded_value();
    // If the value provided is an integer, we bucket it and increment the
    // corresponding bucket.
    if (ValuePart::kIntValue == value.data_case()) {
      counts_[int_bucket_config_->BucketIndex(value.int_value())] += 1;
      return true;
    }

    if (ValuePart::kIntBucketDistribution == value.data_case()) {
      // First, we check that all the indices correspond to valid buckets.
      for (auto iter = value.int_bucket_distribution().counts().begin();
           value.int_bucket_distribution().counts().end() != iter; iter++) {
        if (iter->first > int_bucket_config_->OverflowBucket()) {
          return false;
        }
      }

      for (auto iter = value.int_bucket_distribution().counts().begin();
           value.int_bucket_distribution().counts().end() != iter; iter++) {
        counts_[iter->first] += iter->second;
      }
      return true;
    }
    return false;
  }

  grpc::Status PerformAnalysis(std::vector<ReportRow>* results) override {
    for (const auto& pair : counts_) {
      results->emplace_back();
      HistogramReportRow* row = results->back().mutable_histogram();
      row->mutable_value()->set_index_value(pair.first);
      row->set_count_estimate(pair.second);
      row->set_std_error(0);
      // TODO(azani): Generate labels.
    }
    return grpc::Status::OK;
  }

 private:
  ReportId report_id_;
  cobalt::NoOpEncodingConfig config_;
  std::map<uint32_t, size_t> counts_;
  std::unique_ptr<cobalt::config::IntegerBucketConfig> int_bucket_config_;
};

////////////////////////////////////////////////////////////////////////////
/// HistogramAnalysisEngine methods.
///////////////////////////////////////////////////////////////////////////
HistogramAnalysisEngine::HistogramAnalysisEngine(
    const ReportId& report_id, const ReportVariable* report_variable,
    const MetricPart* metric_part,
    std::shared_ptr<AnalyzerConfig> analyzer_config)
    : report_id_(report_id),
      report_variable_(report_variable),
      metric_part_(metric_part),
      analyzer_config_(analyzer_config) {}

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
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kPerformAnalysisFailure) << message;
    return grpc::Status(grpc::UNIMPLEMENTED, message);
  }

  if (decoders_.size() == 0) {
    std::ostringstream stream;
    stream << "Empty report. No valid observations found for report_id="
           << ReportStore::ToString(report_id_);
    std::string message = stream.str();
    LOG(INFO) << message;
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
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kGetDecoderFailure)
        << "Bad ObservationPart! Contains invalid encoding_config_id "
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
  if (metric_part_->has_int_buckets()) {
    return std::unique_ptr<DecoderAdapter>(new NoOpIntBucketDistributionAdapter(
        report_id_, encoding_config->no_op_encoding(),
        cobalt::config::IntegerBucketConfig::CreateFromProto(
            metric_part_->int_buckets())));
  }

  const IndexLabels* index_labels = nullptr;
  if (report_variable_->has_index_labels()) {
    index_labels = &(report_variable_->index_labels());
  }
  switch (encoding_config->config_case()) {
    case EncodingConfig::kForculus:
      return std::unique_ptr<DecoderAdapter>(
          new ForculusAdapter(report_id_, encoding_config->forculus()));
    case EncodingConfig::kRappor: {
      const RapporCandidateList* rappor_candidates = nullptr;
      if (report_variable_->has_rappor_candidates()) {
        rappor_candidates = &(report_variable_->rappor_candidates());
      } else {
        LOG_STACKDRIVER_COUNT_METRIC(ERROR, kNewDecoderFailure)
            << "HistogramAnalysisEngine: Received an observation with "
               "encoding_config_id="
            << encoding_config->id()
            << " for String RAPPOR but no RAPPOR candidates are "
               "specified for report_id="
            << ReportStore::ToString(report_id_);
      }
      return std::unique_ptr<DecoderAdapter>(new RapporAdapter(
          report_id_, encoding_config->rappor(), rappor_candidates));
    }
    case EncodingConfig::kBasicRappor: {
      return std::unique_ptr<DecoderAdapter>(new BasicRapporAdapter(
          report_id_, encoding_config->basic_rappor(), index_labels));
    }
    case EncodingConfig::kNoOpEncoding: {
      return std::unique_ptr<DecoderAdapter>(new NoOpAdapter(
          report_id_, encoding_config->no_op_encoding(), index_labels));
    }
    default:
      LOG(FATAL) << "Unexpected EncodingConfig type "
                 << encoding_config->config_case();
  }
}

}  // namespace analyzer
}  // namespace cobalt
