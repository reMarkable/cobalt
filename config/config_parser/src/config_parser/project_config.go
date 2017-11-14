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

// This file contains the respresentation for the configuration of a cobalt
// project (See projectConfig) and a way to parse that configuration information
// from a yaml string.

package config_parser

import (
	"config"
	"fmt"
	"github.com/golang/glog"
	"regexp"
	"yamlpb"
)

// Represents the configuration of a single project.
type projectConfig struct {
	customerName  string
	customerId    uint32
	projectName   string
	projectId     uint32
	contact       string
	projectConfig config.CobaltConfig
}

var validMetricPartName = regexp.MustCompile("^[a-zA-Z][_a-zA-Z0-9\\- ]+$")

// Parse the configuration for one project from the yaml string provided into
// the config field in projectConfig.
func parseProjectConfig(y string, c *projectConfig) (err error) {
	if err := yamlpb.UnmarshalString(y, &c.projectConfig); err != nil {
		return fmt.Errorf("Error while parsing yaml: %v", err)
	}

	// Maps metric ids to the metric's index in c.projectConfig.MetricConfigs.
	metrics := map[uint32]uint32{}

	// Set of encoding ids. Used to detect duplicates.
	encodingIds := map[uint32]bool{}

	// Set of report ids. Used to detect duplicates.
	reportIds := map[uint32]bool{}
	for i, e := range c.projectConfig.EncodingConfigs {
		if encodingIds[e.Id] {
			return fmt.Errorf("Encoding id '%v' is repeated in encoding config entry number %v. Encoding ids must be unique.", e.Id, i)
		}
		encodingIds[e.Id] = true
		e.CustomerId = c.customerId
		e.ProjectId = c.projectId
	}

	for i, e := range c.projectConfig.MetricConfigs {
		if _, ok := metrics[e.Id]; ok {
			return fmt.Errorf("Metric id '%v' is repeated in metric config entry number %v. Metric ids must be unique.", e.Id, i)
		}
		metrics[e.Id] = uint32(i)

		if err := validateMetric(*e); err != nil {
			return fmt.Errorf("Error validating metric %v (%v): %v", e.Name, e.Id, err)
		}
		e.CustomerId = c.customerId
		e.ProjectId = c.projectId
	}

	for i, e := range c.projectConfig.ReportConfigs {
		if reportIds[e.Id] {
			return fmt.Errorf("Report id '%v' is repeated in report config entry number %v. Report ids must be unique.", e.Id, i)
		}
		reportIds[e.Id] = true
		e.CustomerId = c.customerId
		e.ProjectId = c.projectId

		if _, ok := metrics[e.MetricId]; !ok {
			return fmt.Errorf("Error validating report %v (%v): There is no metric id %v.", e.Name, e.Id, e.MetricId)
		}

		m := c.projectConfig.MetricConfigs[metrics[e.MetricId]]
		if err := validateReportVariables(*e, *m); err != nil {
			return fmt.Errorf("Error validating report %v (%v): %v", e.Name, e.Id, err)
		}
	}

	return nil
}

func validateMetric(m config.Metric) (err error) {
	for name, v := range m.Parts {
		if v == nil {
			return fmt.Errorf("Metric part '%v' is null. This is not allowed.", name)
		}

		if !validMetricPartName.MatchString(name) {
			return fmt.Errorf("Metric part name '%v' is invalid. Metric part names must match the regular expression '%v'.", name, validNameRegexp)
		}
	}

	return nil
}

// Checks that the report variables are compatible with the specific metric.
func validateReportVariables(c config.ReportConfig, m config.Metric) (err error) {
	if len(c.Variable) == 0 {
		glog.Warningf("Report '%v' (Customer %v, Project %v Id %v) does not have any report variables.", c.Name, c.CustomerId, c.ProjectId, c.Id)
		return nil
	}

	for i, v := range c.Variable {
		if v == nil {
			return fmt.Errorf("Report Variable in position %v is null. This is not allowed.", i)
		}

		// Check that the metric part being referenced can be found.
		p, ok := m.Parts[v.MetricPart]
		if !ok {
			return fmt.Errorf("Metric part '%v' is not present in metric '%v'.", v.MetricPart, m.Name)
		}

		// Checks that if index labels are found, the metric part referred to is an index.
		if v.IndexLabels != nil && len(v.IndexLabels.Labels) > 0 && p.DataType != config.MetricPart_INDEX {
			return fmt.Errorf("Report variable %v has index labels specified "+
				"which implies referring to an index metric part. But metric part '%v'"+
				"of metric '%v' (%v) is of type %v.",
				i, v.MetricPart, m.Name, m.Id, config.MetricPart_DataType_name[int32(p.DataType)])
		}

		// Checks that if RAPPOR candidates are found, the metric part referred to is a string.
		if v.RapporCandidates != nil && len(v.RapporCandidates.Candidates) > 0 && p.DataType != config.MetricPart_STRING {
			return fmt.Errorf("Report variable %v has RAPPOR candidates specified "+
				"which implies referring to a string metric part. But metric part '%v'"+
				"of metric '%v' (%v) is of type %v.",
				i, v.MetricPart, m.Name, m.Id, config.MetricPart_DataType_name[int32(p.DataType)])
		}
	}

	return nil
}
