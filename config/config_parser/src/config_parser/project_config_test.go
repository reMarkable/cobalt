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

import (
	"config"
	proto "github.com/golang/protobuf/proto"
	"reflect"
	"testing"
)

// Basic test for parseProjectConfig.
func TestParseProjectConfig(t *testing.T) {
	y := `
metric_configs:
- id: 1
  name: metric_name
  time_zone_policy: UTC
- id: 2
  name: other_metric_name
  time_zone_policy: LOCAL
encoding_configs:
- id: 1
  basic_rappor:
    prob_0_becomes_1: 0.5
    prob_1_stays_1: 0.5
- id: 2
report_configs:
- id: 1
  metric_id: 1
- id: 2
  metric_id: 1
`
	c := projectConfig{
		customerId: 1,
		projectId:  10,
	}

	if err := parseProjectConfig(y, &c); err != nil {
		t.Error(err)
	}

	e := config.CobaltConfig{
		EncodingConfigs: []*config.EncodingConfig{
			&config.EncodingConfig{
				CustomerId: 1,
				ProjectId:  10,
				Id:         1,
				Config: &config.EncodingConfig_BasicRappor{
					&config.BasicRapporConfig{
						Prob_0Becomes_1: 0.5,
						Prob_1Stays_1:   0.5,
					},
				},
			},
			&config.EncodingConfig{
				CustomerId: 1,
				ProjectId:  10,
				Id:         2,
			},
		},
		MetricConfigs: []*config.Metric{
			&config.Metric{
				CustomerId:     1,
				ProjectId:      10,
				Id:             1,
				Name:           "metric_name",
				TimeZonePolicy: config.Metric_UTC,
			},
			&config.Metric{
				CustomerId:     1,
				ProjectId:      10,
				Id:             2,
				Name:           "other_metric_name",
				TimeZonePolicy: config.Metric_LOCAL,
			},
		},
		ReportConfigs: []*config.ReportConfig{
			&config.ReportConfig{
				CustomerId: 1,
				ProjectId:  10,
				Id:         1,
				MetricId:   1,
			},
			&config.ReportConfig{
				CustomerId: 1,
				ProjectId:  10,
				Id:         2,
				MetricId:   1,
			},
		},
	}

	if !reflect.DeepEqual(e, c.projectConfig) {
		t.Errorf("%v\n!=\n%v", proto.MarshalTextString(&e), proto.MarshalTextString(&c.projectConfig))
	}
}

// Tests that we catch non-unique metric ids.
func TestParseProjectConfigUniqueMetricIds(t *testing.T) {
	y := `
metric_configs:
- id: 1
  name: metric_name
  time_zone_policy: UTC
- id: 1
  name: other_metric_name
  time_zone_policy: LOCAL
encoding_configs:
- id: 1
  basic_rappor:
    prob_0_becomes_1: 0.5
    prob_1_stays_1: 0.5
- id: 2
report_configs:
- id: 1
  metric_id: 5
- id: 2
`

	c := projectConfig{
		customerId: 1,
		projectId:  10,
	}

	if err := parseProjectConfig(y, &c); err == nil {
		t.Error("Accepted non-unique metric id.")
	}
}

// Tests that we catch non-unique encoding ids.
func TestParseProjectConfigUniqueEncodingIds(t *testing.T) {
	y := `
metric_configs:
- id: 1
  name: metric_name
  time_zone_policy: UTC
- id: 2
  name: other_metric_name
  time_zone_policy: LOCAL
encoding_configs:
- id: 1
  basic_rappor:
    prob_0_becomes_1: 0.5
    prob_1_stays_1: 0.5
- id: 1
report_configs:
- id: 1
  metric_id: 5
- id: 2
`

	c := projectConfig{
		customerId: 1,
		projectId:  10,
	}

	if err := parseProjectConfig(y, &c); err == nil {
		t.Error("Accepted non-unique encoding id.")
	}
}

// Tests that we catch non-unique report ids.
func TestParseProjectConfigUniqueReportIds(t *testing.T) {
	y := `
metric_configs:
- id: 1
  name: metric_name
  time_zone_policy: UTC
- id: 2
  name: other_metric_name
  time_zone_policy: LOCAL
encoding_configs:
- id: 1
  basic_rappor:
    prob_0_becomes_1: 0.5
    prob_1_stays_1: 0.5
- id: 2
report_configs:
- id: 1
  metric_id: 5
- id: 1
`

	c := projectConfig{
		customerId: 1,
		projectId:  10,
	}

	if err := parseProjectConfig(y, &c); err == nil {
		t.Error("Accepted non-unique report id.")
	}
}

// Test that a report config with an unknown metric id gets rejected.
func TestParseProjectConfigUnknownMetricIdInReportConfig(t *testing.T) {
	y := `
report_configs:
- id: 1
  metric_id: 10
`
	c := projectConfig{}

	if err := parseProjectConfig(y, &c); err == nil {
		t.Error("Accepted report config with unknown metric id.")
	}
}

// Check that valid report variables are accepted.
func TestValidateReportVariables(t *testing.T) {
	m := config.Metric{
		Parts: map[string]*config.MetricPart{
			"int_part":    &config.MetricPart{DataType: config.MetricPart_INT},
			"string_part": &config.MetricPart{DataType: config.MetricPart_STRING},
			"blob_part":   &config.MetricPart{DataType: config.MetricPart_BLOB},
			"index_part":  &config.MetricPart{DataType: config.MetricPart_INDEX},
		},
	}

	c := config.ReportConfig{
		Variable: []*config.ReportVariable{
			&config.ReportVariable{
				MetricPart: "int_part",
			},
			&config.ReportVariable{
				MetricPart:  "index_part",
				IndexLabels: &config.IndexLabels{Labels: map[uint32]string{0: "zero"}},
			},
			&config.ReportVariable{
				MetricPart:       "string_part",
				RapporCandidates: &config.RapporCandidateList{Candidates: []string{"hello"}},
			},
		},
	}

	if err := validateReportVariables(c, m); err != nil {
		t.Error(err)
	}
}

// Test that a report variable referring to an unknown metric part will be rejected.
func TestValidateReportVariablesUnknownMetricPart(t *testing.T) {
	m := config.Metric{}

	c := config.ReportConfig{
		Variable: []*config.ReportVariable{
			&config.ReportVariable{
				MetricPart: "int_part",
			},
		},
	}

	if err := validateReportVariables(c, m); err == nil {
		t.Error("Report with unknown metric part was accepted.")
	}
}

// Test that if a report variable specifies index labels, the metric part it
// refers to must be of type index.
func TestValidateReportVariablesIndexLablesNonIndexMetric(t *testing.T) {
	m := config.Metric{
		Parts: map[string]*config.MetricPart{
			"int_part": &config.MetricPart{DataType: config.MetricPart_INT},
		},
	}

	c := config.ReportConfig{
		Variable: []*config.ReportVariable{
			&config.ReportVariable{
				MetricPart:  "int_part",
				IndexLabels: &config.IndexLabels{Labels: map[uint32]string{0: "zero"}},
			},
		},
	}

	if err := validateReportVariables(c, m); err == nil {
		t.Error("Report with with index labels specified for a non-index metric part accepted.")
	}
}

// Test that if a report variable specifies rappor candidates, the metric part
// it refers to must be of type string.
func TestValidateReportVarialesRapporCandidatesNonStringMetric(t *testing.T) {
	m := config.Metric{
		Parts: map[string]*config.MetricPart{
			"int_part": &config.MetricPart{DataType: config.MetricPart_INT},
		},
	}

	c := config.ReportConfig{
		Variable: []*config.ReportVariable{
			&config.ReportVariable{
				MetricPart:       "int_part",
				RapporCandidates: &config.RapporCandidateList{Candidates: []string{"alpha"}},
			},
		},
	}

	if err := validateReportVariables(c, m); err == nil {
		t.Error("Report with with rappor candidates specified for a non-string metric part accepted.")
	}
}

func TestValidateMetricNoNilMetricPart(t *testing.T) {
	m := config.Metric{
		Parts: map[string]*config.MetricPart{"int_part": nil},
	}

	if err := validateMetric(m); err == nil {
		t.Error("Metric with nil metric part was accepted.")
	}
}

// Test that metric parts with a name that starts with an underscore are rejected.
func TestValidateMetricInvalidMetricPartName(t *testing.T) {
	m := config.Metric{
		Parts: map[string]*config.MetricPart{"_int_part": &config.MetricPart{}},
	}

	if err := validateMetric(m); err == nil {
		t.Error("Metric with invalid name for metric part was accepted.")
	}
}
