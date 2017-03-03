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

#include "encoder/encoder.h"

#include <glog/logging.h>

#include <ctime>
#include <utility>

#include "algorithms/forculus/forculus_encrypter.h"
#include "algorithms/rappor/rappor_encoder.h"
#include "config/encodings.pb.h"
#include "config/metrics.pb.h"
#include "util/datetime_util.h"

namespace cobalt {
namespace encoder {

using forculus::ForculusEncrypter;
using rappor::BasicRapporEncoder;
using rappor::RapporEncoder;
using util::TimeToDayIndex;

Encoder::Encoder(std::shared_ptr<ProjectContext> project,
                 ClientSecret client_secret)
    : customer_id_(project->customer_id()),
      project_id_(project->project_id()),
      project_(project),
      client_secret_(std::move(client_secret)) {}

Encoder::Status Encoder::EncodeForculus(
    uint32_t metric_id, uint32_t encoding_config_id,
    MetricPart::DataType data_type, const ValuePart& value,
    const EncodingConfig* encoding_config, const std::string& part_name,
    uint32_t day_index, ObservationPart* observation_part) {
  switch (data_type) {
    case MetricPart::INT: {
      VLOG(3) << "Forculus doesn't support INTs: (" << customer_id_ << ", "
              << project_id_ << ", " << encoding_config_id << ")";
      return kInvalidArguments;
    }
    default:
      break;
  }
  ForculusObservation* forculus_observation =
      observation_part->mutable_forculus();
  ForculusEncrypter forculus_encrypter(encoding_config->forculus(),
                                       customer_id_, project_id_, metric_id,
                                       part_name, client_secret_);

  switch (
      forculus_encrypter.EncryptValue(value, day_index, forculus_observation)) {
    case ForculusEncrypter::kOK:
      return kOK;

    case ForculusEncrypter::kInvalidConfig:
      return kInvalidConfig;

    case ForculusEncrypter::kEncryptionFailed:
      VLOG(3) << "Forculs encryption failed for encoding (" << customer_id_
              << ", " << project_id_ << ", " << encoding_config_id << ")";
      return kEncodingFailed;
  }
}

Encoder::Status Encoder::EncodeRappor(uint32_t metric_id,
                                      uint32_t encoding_config_id,
                                      MetricPart::DataType data_type,
                                      const ValuePart& value,
                                      const EncodingConfig* encoding_config,
                                      const std::string& part_name,
                                      ObservationPart* observation_part) {
  switch (data_type) {
    case MetricPart::INT: {
      VLOG(3) << "RAPPOR doesn't support INTs: (" << customer_id_ << ", "
              << project_id_ << ", " << encoding_config_id << ")";
      return kInvalidArguments;
    }
    case MetricPart::BLOB: {
      VLOG(3) << "RAPPOR doesn't support Blobs: (" << customer_id_ << ", "
              << project_id_ << ", " << encoding_config_id << ")";
      return kInvalidArguments;
    }
    default:
      break;
  }
  RapporObservation* rappor_observation = observation_part->mutable_rappor();
  RapporEncoder rappor_encoder(encoding_config->rappor(), client_secret_);
  switch (rappor_encoder.Encode(value, rappor_observation)) {
    case rappor::kOK:
      return kOK;

    case rappor::kInvalidConfig:
      return kInvalidConfig;

    case rappor::kInvalidInput:
      VLOG(3) << "Invalid arguments to RapporEncoder for encoding ("
              << customer_id_ << ", " << project_id_ << ", "
              << encoding_config_id << ")";
      return kInvalidArguments;
  }
}

Encoder::Status Encoder::EncodeBasicRappor(
    uint32_t metric_id, uint32_t encoding_config_id,
    MetricPart::DataType data_type, const ValuePart& value,
    const EncodingConfig* encoding_config, const std::string& part_name,
    ObservationPart* observation_part) {
  switch (data_type) {
    case MetricPart::BLOB: {
      VLOG(3) << "Basic RAPPOR doesn't support Blobs: (" << customer_id_ << ", "
              << project_id_ << ", " << encoding_config_id << ")";
      return kInvalidArguments;
    }
    default:
      break;
  }
  BasicRapporObservation* basic_rappor_observation =
      observation_part->mutable_basic_rappor();
  BasicRapporEncoder basic_rappor_encoder(encoding_config->basic_rappor(),
                                          client_secret_);
  switch (basic_rappor_encoder.Encode(value, basic_rappor_observation)) {
    case rappor::kOK:
      return kOK;

    case rappor::kInvalidConfig:
      return kInvalidConfig;

    case rappor::kInvalidInput:
      VLOG(3) << "Invalid arguments to BasicRapporEncoder for encoding ("
              << customer_id_ << ", " << project_id_ << ", "
              << encoding_config_id << ")";
      return kInvalidArguments;
  }
}

Encoder::Result Encoder::EncodeString(uint32_t metric_id,
                                      uint32_t encoding_config_id,
                                      const std::string& string_value) {
  Value value;
  // An empty part name is a signal to the function Encoder::Encode() that
  // the metric has only a single part whose name should be looked up.
  value.AddStringPart(encoding_config_id, "", string_value);
  return Encode(metric_id, value);
}

Encoder::Result Encoder::EncodeInt(uint32_t metric_id,
                                   uint32_t encoding_config_id,
                                   int64_t int_value) {
  Value value;
  // An empty part name means the metric has only a single part.
  value.AddIntPart(encoding_config_id, "", int_value);
  return Encode(metric_id, value);
}

Encoder::Result Encoder::EncodeBlob(uint32_t metric_id,
                                    uint32_t encoding_config_id,
                                    const void* data, size_t num_bytes) {
  Value value;
  // An empty part name means the metric has only a single part.
  value.AddBlobPart(encoding_config_id, "", data, num_bytes);
  return Encode(metric_id, value);
}

Encoder::Result Encoder::Encode(uint32_t metric_id, const Value& value) {
  Result result;

  // Get the Metric.
  const Metric* metric = project_->Metric(metric_id);
  if (!metric) {
    // No such metric.
    VLOG(3) << "No such metric: (" << customer_id_ << ", " << project_id_
            << ", " << metric_id << ")";
    result.status = kInvalidArguments;
    return result;
  }

  // Check that the number of values provided equals the number of metric
  // parts.
  if (metric->parts().size() != value.parts_.size()) {
    VLOG(3) << "Metric (" << customer_id_ << ", " << project_id_ << ", "
            << metric_id << ") does not have " << value.parts_.size()
            << " part(s)";
    result.status = kInvalidArguments;
    return result;
  }

  // Compute the day_index.
  time_t current_time = current_time_;
  if (current_time <= 0) {
    // Use the real clock if we have not been given a static value for
    // current_time.
    current_time = std::time(nullptr);
  }
  uint32_t day_index = TimeToDayIndex(current_time, metric->time_zone_policy());
  if (day_index == UINT32_MAX) {
    // Invalid Metric: No time_zone_policy.
    VLOG(3) << "TimeZonePolicy unset for metric: (" << customer_id_ << ", "
            << project_id_ << ", " << metric_id << ")";
    result.status = kInvalidConfig;
    return result;
  }

  // Create a new Observation and ObservationMetadata.
  result.observation.reset(new Observation());
  result.metadata.reset(new ObservationMetadata());
  result.metadata->set_customer_id(customer_id_);
  result.metadata->set_project_id(project_id_);
  result.metadata->set_metric_id(metric_id);
  result.metadata->set_day_index(day_index);

  // Iterate through the provided values.
  for (const auto& key_value : value.parts_) {
    std::string part_name = key_value.first;
    const Value::ValuePartData& value_part_data = key_value.second;

    // Find the metric part with the specified name.
    google::protobuf::Map<std::basic_string<char>,
                          cobalt::MetricPart>::const_iterator
        metric_part_iterator;
    if (part_name.empty() && metric->parts().size() == 1) {
      // Special case: If there is only one metric part and the provided
      // part_name is the empty string then use that single metric part.
      metric_part_iterator = metric->parts().begin();
      part_name = metric_part_iterator->first;

    } else {
      metric_part_iterator = metric->parts().find(part_name);
      if (metric_part_iterator == metric->parts().cend()) {
        VLOG(3) << "Metric (" << customer_id_ << ", " << project_id_ << ", "
                << metric_id << ") does not have a part named " << part_name
                << ".";
        result.status = kInvalidArguments;
        return result;
      }
    }
    const MetricPart& metric_part = metric_part_iterator->second;

    // Check that the data type of the ValuePart matches the data_type of the
    // MetricPart.
    if (metric_part.data_type() != value_part_data.data_type) {
      VLOG(3) << "Metric part (" << customer_id_ << ", " << project_id_ << ", "
              << metric_id << ")-" << part_name << " is not of type "
              << value_part_data.data_type << ".";
      result.status = kInvalidArguments;
      return result;
    }

    // Get the EncodingConfig
    const EncodingConfig* encoding_config =
        project_->EncodingConfig(value_part_data.encoding_config_id);
    if (!encoding_config) {
      // No such encoding config.
      VLOG(3) << "No such encoding config: (" << customer_id_ << ", "
              << project_id_ << ", " << value_part_data.encoding_config_id
              << ")";
      result.status = kInvalidArguments;
      return result;
    }

    // Add an ObservationPart to the Observation with the part_name.
    ObservationPart& observation_part =
        (*result.observation->mutable_parts())[part_name];
    observation_part.set_encoding_config_id(value_part_data.encoding_config_id);

    // Perform the encoding.
    Status status = kOK;
    switch (encoding_config->config_case()) {
      case EncodingConfig::kForculus: {
        status = EncodeForculus(metric_id, value_part_data.encoding_config_id,
                                value_part_data.data_type,
                                value_part_data.value_part, encoding_config,
                                part_name, day_index, &observation_part);
        break;
      }

      case EncodingConfig::kRappor: {
        status =
            EncodeRappor(metric_id, value_part_data.encoding_config_id,
                         value_part_data.data_type, value_part_data.value_part,
                         encoding_config, part_name, &observation_part);
        break;
      }

      case EncodingConfig::kBasicRappor: {
        status = EncodeBasicRappor(
            metric_id, value_part_data.encoding_config_id,
            value_part_data.data_type, value_part_data.value_part,
            encoding_config, part_name, &observation_part);
        break;
      }

      default:
        status = kInvalidConfig;
        break;
    }
    if (status != kOK) {
      result.status = status;
      return result;
    }
  }
  result.status = kOK;
  return result;
}

ValuePart& Encoder::Value::AddPart(uint32_t encoding_config_id,
                                   const std::string& part_name,
                                   MetricPart::DataType data_type) {
  // emplace() returns a pair whose first element is an iterator over
  // pairs whose second element is a ValuePartData.
  return parts_.emplace(part_name, ValuePartData(encoding_config_id, data_type))
      .first->second.value_part;
}

void Encoder::Value::AddStringPart(uint32_t encoding_config_id,
                                   const std::string& part_name,
                                   const std::string& value) {
  AddPart(encoding_config_id, part_name, MetricPart::STRING)
      .set_string_value(value);
}

void Encoder::Value::AddIntPart(uint32_t encoding_config_id,
                                const std::string& part_name, int64_t value) {
  AddPart(encoding_config_id, part_name, MetricPart::INT).set_int_value(value);
}

void Encoder::Value::AddBlobPart(uint32_t encoding_config_id,
                                 const std::string& part_name, const void* data,
                                 size_t num_bytes) {
  AddPart(encoding_config_id, part_name, MetricPart::BLOB)
      .set_blob_value(data, num_bytes);
}

}  // namespace encoder
}  // namespace cobalt
