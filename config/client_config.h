// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CONFIG_CLIENT_CONFIG_H_
#define COBALT_CONFIG_CLIENT_CONFIG_H_

#include <memory>
#include <string>
#include <utility>

#include "config/cobalt_config.pb.h"
#include "config/encoding_config.h"
#include "config/encodings.pb.h"
#include "config/metric_config.h"
#include "config/metrics.pb.h"

namespace cobalt {
namespace config {

// ClientConfig provides a convenient interface to the Cobalt configuration
// system that is intended to be used by the Encoder client.
class ClientConfig {
 public:
  // Constructs and returns an instance of ClientConfig by first parsing
  // a CobaltConfig proto message from |cobalt_config_base64|, which should
  // contain the Base64 encoding of the bytes of the binary serialization of
  // such a message, and then extracting the Metrics and EncodingConfigs from
  // that.
  static std::unique_ptr<ClientConfig> CreateFromCobaltConfigBase64(
      const std::string& cobalt_config_base64);

  // Constructs and returns an instance of ClientConfig by first parsing
  // a CobaltConfig proto message from |cobalt_config_bytes|, which should
  // contain the bytes of the binary serialization of such a message, and then
  // extracting the Metrics and EncodingConfigs from that.
  static std::unique_ptr<ClientConfig> CreateFromCobaltConfigBytes(
      const std::string& cobalt_config_bytes);

  // Constructs and returns an instance of ClientConfig by swapping all of
  // the Metrics and EncodingConfigs from the given |cobalt_config|.
  static std::unique_ptr<ClientConfig> CreateFromCobaltConfig(
      CobaltConfig* cobalt_config);

  // Returns the EncodingConfig with the given ID triple, or nullptr if there is
  // no such EncodingConfig. The caller does not take ownership of the returned
  // pointer.
  const EncodingConfig* EncodingConfig(uint32_t customer_id,
                                       uint32_t project_id,
                                       uint32_t encoding_config_id);

  // Returns the Metric with the given ID triple, or nullptr if there is
  // no such Metric. The caller does not take ownership of the returned
  // pointer.
  const Metric* Metric(uint32_t customer_id, uint32_t project_id,
                       uint32_t metric_id);

 private:
  // Constructs an ClientConfig that wraps the given registries.
  ClientConfig(std::shared_ptr<config::EncodingRegistry> encoding_configs,
               std::shared_ptr<config::MetricRegistry> metrics);

  std::shared_ptr<config::EncodingRegistry> encoding_configs_;
  std::shared_ptr<config::MetricRegistry> metrics_;
};

}  // namespace config
}  // namespace cobalt

#endif  // COBALT_CONFIG_CLIENT_CONFIG_H_
