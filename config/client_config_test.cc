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
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/crypto_util/base64.h"

namespace cobalt {
namespace config {

namespace {
void AddMetric(int customer_id, int project_id, int id,
               CobaltConfig* cobalt_config) {
  Metric* metric = cobalt_config->add_metric_configs();
  metric->set_customer_id(customer_id);
  metric->set_project_id(project_id);
  metric->set_id(id);
}

void AddMetric(int id, CobaltConfig* cobalt_config) {
  AddMetric(id, id, id, cobalt_config);
}

void AddEncodingConfig(int customer_id, int project_id, int id,
                       CobaltConfig* cobalt_config) {
  EncodingConfig* encoding_config = cobalt_config->add_encoding_configs();
  encoding_config->set_customer_id(customer_id);
  encoding_config->set_project_id(project_id);
  encoding_config->set_id(id);
}

void AddEncodingConfig(int id, CobaltConfig* cobalt_config) {
  AddEncodingConfig(id, id, id, cobalt_config);
}

}  // namespace

TEST(ClientConfigTest, ValidateSingleProjectConfig) {
  CobaltConfig cobalt_config;
  AddMetric(1, 1, 42, &cobalt_config);
  AddMetric(1, 1, 43, &cobalt_config);
  AddEncodingConfig(1, 1, 42, &cobalt_config);
  AddEncodingConfig(1, 1, 43, &cobalt_config);
  EXPECT_FALSE(ClientConfig::ValidateSingleProjectConfig(
      cobalt_config.metric_configs(), 1, 2));
  EXPECT_FALSE(ClientConfig::ValidateSingleProjectConfig(
      cobalt_config.encoding_configs(), 1, 2));
  EXPECT_FALSE(ClientConfig::ValidateSingleProjectConfig(
      cobalt_config.metric_configs(), 2, 1));
  EXPECT_FALSE(ClientConfig::ValidateSingleProjectConfig(
      cobalt_config.encoding_configs(), 2, 1));
  EXPECT_FALSE(ClientConfig::ValidateSingleProjectConfig(
      cobalt_config.metric_configs(), 2, 2));
  EXPECT_FALSE(ClientConfig::ValidateSingleProjectConfig(
      cobalt_config.encoding_configs(), 2, 2));
  EXPECT_TRUE(ClientConfig::ValidateSingleProjectConfig(
      cobalt_config.metric_configs(), 1, 1));
  EXPECT_TRUE(ClientConfig::ValidateSingleProjectConfig(
      cobalt_config.encoding_configs(), 1, 1));
}

TEST(ClientConfigTest, CreateFromCobaltProjectConfigBytesValidConfig) {
  std::string cobalt_config_bytes;
  CobaltConfig cobalt_config;
  AddMetric(1, 1, 42, &cobalt_config);
  AddMetric(1, 1, 43, &cobalt_config);
  AddEncodingConfig(1, 1, 42, &cobalt_config);
  AddEncodingConfig(1, 1, 43, &cobalt_config);
  ASSERT_TRUE(cobalt_config.SerializeToString(&cobalt_config_bytes));
  auto client_config_project_id_pair =
      ClientConfig::CreateFromCobaltProjectConfigBytes(cobalt_config_bytes);
  auto client_config = std::move(client_config_project_id_pair.first);
  ASSERT_NE(nullptr, client_config);
  EXPECT_EQ(1u, client_config_project_id_pair.second);
  EXPECT_EQ(nullptr, client_config->GetEncodingConfig(1, 1, 41));
  EXPECT_NE(nullptr, client_config->GetEncodingConfig(1, 1, 42));
  EXPECT_NE(nullptr, client_config->GetEncodingConfig(1, 1, 43));
  EXPECT_EQ(nullptr, client_config->GetMetric(1, 1, 41));
  EXPECT_NE(nullptr, client_config->GetMetric(1, 1, 42));
  EXPECT_NE(nullptr, client_config->GetMetric(1, 1, 43));
}

TEST(ClientConfigTest, CreateFromCobaltProjectConfigBytesInvalidConfig) {
  std::string cobalt_config_bytes;
  CobaltConfig cobalt_config;
  AddMetric(1, 1, 42, &cobalt_config);
  AddMetric(1, 1, 43, &cobalt_config);
  AddEncodingConfig(1, 2, 42, &cobalt_config);
  AddEncodingConfig(1, 2, 43, &cobalt_config);
  ASSERT_TRUE(cobalt_config.SerializeToString(&cobalt_config_bytes));
  auto client_config_project_id_pair =
      ClientConfig::CreateFromCobaltProjectConfigBytes(cobalt_config_bytes);
  auto client_config = std::move(client_config_project_id_pair.first);
  ASSERT_EQ(nullptr, client_config);
}

TEST(ClientConfigTest, CreateFromCobaltConfigBytes) {
  std::string cobalt_config_bytes;
  CobaltConfig cobalt_config;
  AddMetric(42, &cobalt_config);
  AddMetric(43, &cobalt_config);
  AddEncodingConfig(42, &cobalt_config);
  AddEncodingConfig(43, &cobalt_config);
  ASSERT_TRUE(cobalt_config.SerializeToString(&cobalt_config_bytes));
  auto client_config =
      ClientConfig::CreateFromCobaltConfigBytes(cobalt_config_bytes);
  ASSERT_NE(nullptr, client_config);
  EXPECT_EQ(nullptr, client_config->GetEncodingConfig(41, 41, 41));
  EXPECT_NE(nullptr, client_config->GetEncodingConfig(42, 42, 42));
  EXPECT_NE(nullptr, client_config->GetEncodingConfig(43, 43, 43));
  EXPECT_EQ(nullptr, client_config->GetMetric(41, 41, 41));
  EXPECT_NE(nullptr, client_config->GetMetric(42, 42, 42));
  EXPECT_NE(nullptr, client_config->GetMetric(43, 43, 43));
}

TEST(ClientConfigTest, CreateFromCobaltConfigBase64) {
  std::string cobalt_config_bytes;
  CobaltConfig cobalt_config;
  AddMetric(42, &cobalt_config);
  AddMetric(43, &cobalt_config);
  AddEncodingConfig(42, &cobalt_config);
  AddEncodingConfig(43, &cobalt_config);
  ASSERT_TRUE(cobalt_config.SerializeToString(&cobalt_config_bytes));
  std::string cobalt_config_base64;
  crypto::Base64Encode(cobalt_config_bytes, &cobalt_config_base64);
  auto client_config =
      ClientConfig::CreateFromCobaltConfigBase64(cobalt_config_base64);
  ASSERT_NE(nullptr, client_config);
  EXPECT_EQ(nullptr, client_config->GetEncodingConfig(41, 41, 41));
  EXPECT_NE(nullptr, client_config->GetEncodingConfig(42, 42, 42));
  EXPECT_NE(nullptr, client_config->GetEncodingConfig(43, 43, 43));
  EXPECT_EQ(nullptr, client_config->GetMetric(41, 41, 41));
  EXPECT_NE(nullptr, client_config->GetMetric(42, 42, 42));
  EXPECT_NE(nullptr, client_config->GetMetric(43, 43, 43));
}

}  // namespace config
}  // namespace cobalt
