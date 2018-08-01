// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config/client_config.h"

#include <memory>
#include <string>
#include <utility>

#include "./logging.h"
#include "config/cobalt_config.pb.h"
#include "config/encoding_config.h"
#include "config/encodings.pb.h"
#include "config/metric_config.h"
#include "config/metrics.pb.h"
#include "util/crypto_util/base64.h"

namespace cobalt {
namespace config {

namespace {
std::string ErrorMessage(Status status) {
  switch (status) {
    case kOK:
      return "No error";

    case kFileOpenError:
      return "Unable to open file: ";

    case kParsingError:
      return "Error while parsing file: ";

    case kDuplicateRegistration:
      return "Duplicate ID found in file: ";

    default:
      return "Unknown problem with: ";
  }
}
}  // namespace

std::unique_ptr<ClientConfig> ClientConfig::CreateFromCobaltConfigBase64(
    const std::string& cobalt_config_base64) {
  std::string cobalt_config_bytes;
  if (!crypto::Base64Decode(cobalt_config_base64, &cobalt_config_bytes)) {
    LOG(ERROR) << "Unable to parse the provided string as base-64";
    return nullptr;
  }
  return CreateFromCobaltConfigBytes(cobalt_config_bytes);
}

std::unique_ptr<ClientConfig> ClientConfig::CreateFromCobaltConfigBytes(
    const std::string& cobalt_config_bytes) {
  CobaltConfig cobalt_config;
  if (!cobalt_config.ParseFromString(cobalt_config_bytes)) {
    LOG(ERROR) << "Unable to parse a CobaltConfig from the provided bytes.";
    return nullptr;
  }
  return CreateFromCobaltConfig(&cobalt_config);
}

template <class Config>
bool ClientConfig::ValidateSingleProjectConfig(
    const ::google::protobuf::RepeatedPtrField<Config>& configs,
    uint32_t customer_id, uint32_t project_id) {
  for (int i = 0; i < configs.size(); i++) {
    if (configs[i].customer_id() != customer_id ||
        configs[i].project_id() != project_id) {
      return false;
    }
  }

  return true;
}

std::pair<std::unique_ptr<ClientConfig>, uint32_t>
ClientConfig::CreateFromCobaltProjectConfigBytes(
    const std::string& cobalt_config_bytes) {
  uint32_t customer_id = 0;
  uint32_t project_id = 0;

  CobaltConfig cobalt_config;
  if (!cobalt_config.ParseFromString(cobalt_config_bytes)) {
    LOG(ERROR) << "Unable to parse a CobaltConfig from the provided bytes.";
    return std::make_pair(nullptr, project_id);
  }

  if (cobalt_config.metric_configs_size() > 0) {
    customer_id = cobalt_config.metric_configs()[0].customer_id();
    project_id = cobalt_config.metric_configs()[0].project_id();
  } else if (cobalt_config.encoding_configs_size() > 0) {
    customer_id = cobalt_config.encoding_configs()[0].customer_id();
    project_id = cobalt_config.encoding_configs()[0].project_id();
  }

  // Validate configs
  if (!ValidateSingleProjectConfig<::cobalt::Metric>(
          cobalt_config.metric_configs(), customer_id, project_id)) {
    LOG(ERROR) << "More than one customer_id or project_id found.";
    return std::make_pair(nullptr, project_id);
  }

  if (!ValidateSingleProjectConfig<::cobalt::EncodingConfig>(
          cobalt_config.encoding_configs(), customer_id, project_id)) {
    LOG(ERROR) << "More than one customer_id or project_id found.";
    return std::make_pair(nullptr, project_id);
  }

  return std::make_pair(CreateFromCobaltConfig(&cobalt_config), project_id);
}

std::unique_ptr<ClientConfig> ClientConfig::CreateFromCobaltConfig(
    CobaltConfig* cobalt_config) {
  RegisteredEncodings registered_encodings;
  registered_encodings.mutable_element()->Swap(
      cobalt_config->mutable_encoding_configs());
  auto encodings = EncodingRegistry::TakeFrom(&registered_encodings, nullptr);
  if (encodings.second != config::kOK) {
    LOG(ERROR) << "Invalid EncodingConfigs. " << ErrorMessage(encodings.second);
    return std::unique_ptr<ClientConfig>(nullptr);
  }

  RegisteredMetrics registered_metrics;
  registered_metrics.mutable_element()->Swap(
      cobalt_config->mutable_metric_configs());
  auto metrics = MetricRegistry::TakeFrom(&registered_metrics, nullptr);
  if (metrics.second != config::kOK) {
    LOG(ERROR) << "Error getting Metrics from registry. "
               << ErrorMessage(metrics.second);
    return std::unique_ptr<ClientConfig>(nullptr);
  }

  return std::unique_ptr<ClientConfig>(new ClientConfig(
      std::shared_ptr<config::EncodingRegistry>(encodings.first.release()),
      std::shared_ptr<config::MetricRegistry>(metrics.first.release())));
}

const EncodingConfig* ClientConfig::GetEncodingConfig(
    uint32_t customer_id, uint32_t project_id, uint32_t encoding_config_id) {
  return encoding_configs_->Get(customer_id, project_id, encoding_config_id);
}

const Metric* ClientConfig::GetMetric(uint32_t customer_id, uint32_t project_id,
                                   uint32_t metric_id) {
  return metrics_->Get(customer_id, project_id, metric_id);
}

ClientConfig::ClientConfig(
    std::shared_ptr<config::EncodingRegistry> encoding_configs,
    std::shared_ptr<config::MetricRegistry> metrics)
    : encoding_configs_(encoding_configs), metrics_(metrics) {}

}  // namespace config
}  // namespace cobalt
