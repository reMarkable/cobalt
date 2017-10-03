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

package config_parser

import "config"

type projectConfig struct {
	customerName    string
	customerId      uint32
	projectName     string
	projectId       uint32
	contact         string
	metricConfigs   []config.Metric
	encodingConfigs []config.EncodingConfig
	reportConfigs   []config.ReportConfig
}
