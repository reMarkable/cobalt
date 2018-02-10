// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_parser

import (
	"fmt"
	"testing"
)

type memConfigReader struct {
	customers string
	projects  map[string]string
}

func (r memConfigReader) Customers() (string, error) {
	return r.customers, nil
}

func (r memConfigReader) Project(customerName string, projectName string) (string, error) {
	key := customerName + "|" + projectName
	yaml, ok := r.projects[key]
	if !ok {
		return yaml, fmt.Errorf("Project could not be read!")
	}
	return yaml, nil
}

func (r *memConfigReader) SetProject(customerName string, projectName string, yaml string) {
	if r.projects == nil {
		r.projects = map[string]string{}
	}
	key := customerName + "|" + projectName
	r.projects[key] = yaml
}

const customersYaml = `
- customer_name: fuchsia
  customer_id: 1
  projects:
    - name: ledger
      id: 100
      contact: bob
    - name: module_usage_tracking
      id: 101
      contact: bob
- customer_name: test_customer
  customer_id: 100
  projects:
    - name: test_project
      id: 50
      contact: bob
`

const projectConfigYaml = `
metric_configs:
- id: 1
  name: "Daily rare event counts"
  description: "Daily counts of several events that are expected to occur rarely if ever."
  time_zone_policy: UTC
  parts:
    "Event name":
      description: "Which rare event occurred?"
- id: 2
  name: "Module views"
  description: "Tracks each incidence of viewing a module by its URL."
  time_zone_policy: UTC
  parts:
    "url":
      description: "The URL of the module being launched."

encoding_configs:
- id: 1
  basic_rappor:
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    string_categories:
      category:
      - "Ledger-startup"
      - "Commits-received-out-of-order"
      - "Commits-merged"
      - "Merged-commits-merged"
- id: 2
  forculus:
    threshold: 2
    epoch_type: MONTH

report_configs:
- id: 1
  name: "Fuchsia Ledger Daily Rare Events"
  description: "A daily report of events that are expected to happen rarely."
  metric_id: 1
  variable:
  - metric_part: "Event name"
  scheduling:
    report_finalization_days: 3
    aggregation_epoch_type: DAY
  export_configs:
  - csv: {}
    gcs:
      bucket: "fuchsia-cobalt-reports-p2-test-app"

- id: 2
  name: "Fuchsia Module Daily Launch Counts"
  description: "A daily report of the daily counts of module launches by URL."
  metric_id: 2
  variable:
  - metric_part: "url"
  scheduling:
    report_finalization_days: 3
    aggregation_epoch_type: DAY
  export_configs:
  - csv: {}
    gcs:
      bucket: "fuchsia-cobalt-reports-p2-test-app"
`

// Tests the readProjectConfig function's basic functionality.
func TestReadProjectConfig(t *testing.T) {
	r := memConfigReader{}
	r.SetProject("customer", "project", projectConfigYaml)
	c := projectConfig{
		customerName: "customer",
		customerId:   10,
		projectName:  "project",
		projectId:    5,
	}
	if err := readProjectConfig(r, &c); err != nil {
		t.Errorf("Error reading project config: %v", err)
	}

	if 2 != len(c.projectConfig.EncodingConfigs) {
		t.Errorf("Unexpected number of encoding configs: %v", len(c.projectConfig.EncodingConfigs))
	}
	if 2 != len(c.projectConfig.MetricConfigs) {
		t.Errorf("Unexpected number of metric configs: %v", len(c.projectConfig.MetricConfigs))
	}
	if 2 != len(c.projectConfig.ReportConfigs) {
		t.Errorf("Unexpected number of report configs: %v", len(c.projectConfig.ReportConfigs))
	}
}

// Tests the readConfig function's basic functionality.
func TestReadConfig(t *testing.T) {
	r := memConfigReader{
		customers: customersYaml}
	r.SetProject("fuchsia", "ledger", projectConfigYaml)
	r.SetProject("fuchsia", "module_usage_tracking", projectConfigYaml)
	r.SetProject("test_customer", "test_project", projectConfigYaml)
	l := []projectConfig{}
	if err := readConfig(r, &l); err != nil {
		t.Errorf("Error reading project config: %v", err)
	}

	if 3 != len(l) {
		t.Errorf("Expected 3 customers. Got %v.", len(l))
	}

	for _, c := range l {
		if 2 != len(c.projectConfig.EncodingConfigs) {
			t.Errorf("Unexpected number of encoding configs for %v: %v", c.projectName, len(c.projectConfig.EncodingConfigs))
		}
		if 2 != len(c.projectConfig.MetricConfigs) {
			t.Errorf("Unexpected number of metric configs for %v: %v", c.projectName, len(c.projectConfig.MetricConfigs))
		}
		if 2 != len(c.projectConfig.ReportConfigs) {
			t.Errorf("Unexpected number of report configs for %v: %v", c.projectName, len(c.projectConfig.ReportConfigs))
		}
	}
}
