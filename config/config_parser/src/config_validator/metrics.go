// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"config"
	"fmt"
	"regexp"
)

// Note that when reports are serialized to CSV, the column headers used are
// derived from metric part names. Consequently, if this regular expression
// is modified then the code in the function
// EscapeMetricPartNameForCSVColumHeader() in
// //analyzer/report_master/report_serializer.cc must be modified.
var validMetricPartName = regexp.MustCompile("^[a-zA-Z][_a-zA-Z0-9\\- ]+$")

func validateConfiguredMetrics(config *config.CobaltConfig) (err error) {
	// Set of metric ids. Used to detect duplicates.
	metricIds := map[string]bool{}

	for i, metric := range config.MetricConfigs {
		metricKey := formatId(metric.CustomerId, metric.ProjectId, metric.Id)

		if metricIds[metricKey] {
			return fmt.Errorf("Metric id %s is repeated in metric config entry number %v. Metric ids must be unique.", metricKey, i+1)
		}
		metricIds[metricKey] = true

		if err = validateMetric(metric); err != nil {
			return fmt.Errorf("Error validating metric %v (%v): %v", metric.Name, metric.Id, err)
		}
	}
	return nil
}

func validateMetric(m *config.Metric) (err error) {
	if m.Id == 0 {
		return fmt.Errorf("Metric id '0' is invalid.")
	}

	for name, v := range m.Parts {
		if v == nil {
			return fmt.Errorf("Metric part '%v' is null. This is not allowed.", name)
		}

		if !validMetricPartName.MatchString(name) {
			return fmt.Errorf("Metric part name '%v' is invalid. Metric part names must match the regular expression '%v'.", name, validMetricPartName)
		}
	}
	return nil
}
