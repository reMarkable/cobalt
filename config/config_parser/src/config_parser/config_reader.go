// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements reading the Cobalt configuration from a directory.
// See ReadConfigFromDir for details.

package config_parser

import (
	"config"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
)

// ReadConfigFromDir reads the whole configuration for Cobalt from a directory on the file system.
// It is assumed that <rootDir>/projects.yaml contains the customers and projects list. (see project_list.go)
// It is assumed that <rootDir>/<customerName>/<projectName>/config.yaml
// contains the configuration for a project. (see project_config.go)
func ReadConfigFromDir(rootDir string) (c config.CobaltConfig, err error) {
	r, err := newConfigReaderForDir(rootDir)
	if err != nil {
		return c, err
	}

	l := []projectConfig{}
	if err := readConfig(r, &l); err != nil {
		return c, err
	}

	return mergeConfigs(l), nil
}

// configReader is an interface that returns configuration data in the yaml format.
type configReader interface {
	// Returns the yaml representation of the customer and project list.
	// See project_list.go
	Customers() (string, error)
	// Returns the yaml representation of the configuration for a particular project.
	// See project_config.go
	Project(customerName string, projectName string) (string, error)
}

// configDirReader is an implementation of configReader where the configuration
// data is stored in configDir.
type configDirReader struct {
	configDir string
}

// newConfigReaderForDir returns a configReader which will read the cobalt
// configuration stored in the provided directory.
func newConfigReaderForDir(configDir string) (r configReader, err error) {
	info, err := os.Stat(configDir)
	if err != nil {
		return nil, err
	}

	if !info.IsDir() {
		return nil, fmt.Errorf("%v is not a directory.", configDir)
	}

	return &configDirReader{configDir: configDir}, nil
}

func (r *configDirReader) Customers() (string, error) {
	// The customer and project list is at <rootDir>/projects.yaml
	customerList, err := ioutil.ReadFile(filepath.Join(r.configDir, "projects.yaml"))
	if err != nil {
		return "", err
	}
	return string(customerList), nil
}

func (r *configDirReader) Project(customerName string, projectName string) (string, error) {
	// A project's config is at <rootDir>/<customerName>/<projectName>/config.yaml
	projectConfig, err := ioutil.ReadFile(filepath.Join(r.configDir, customerName, projectName, "config.yaml"))
	if err != nil {
		return "", err
	}
	return string(projectConfig), nil
}

// readConfig reads and parses the configuration for all projects from a configReader.
func readConfig(r configReader, l *[]projectConfig) (err error) {
	// First, we get and parse the customer list.
	customerListYaml, err := r.Customers()
	if err != nil {
		return err
	}

	if err = parseCustomerList(customerListYaml, l); err != nil {
		return err
	}

	// Then, based on the customer list, we read and parse all the project configs.
	for i, _ := range *l {
		c := &((*l)[i])
		if err = readProjectConfig(r, c); err != nil {
			return fmt.Errorf("Error reading config for %v %v: %v", c.customerName, c.projectName, err)
		}
	}

	return nil
}

// readProjectConfig reads the configuration of a particular project.
func readProjectConfig(r configReader, c *projectConfig) (err error) {
	configYaml, err := r.Project(c.customerName, c.projectName)
	if err != nil {
		return err
	}
	return parseProjectConfig(configYaml, c)
}

// mergeConfigs accepts a list of projectConfigs each of which contains the
// encoding, metric and report configs for a particular project and aggregates
// all those into a single CobaltConfig proto.
func mergeConfigs(l []projectConfig) (s config.CobaltConfig) {
	for _, c := range l {
		s.EncodingConfigs = append(s.EncodingConfigs, c.projectConfig.EncodingConfigs...)
		s.MetricConfigs = append(s.MetricConfigs, c.projectConfig.MetricConfigs...)
		s.ReportConfigs = append(s.ReportConfigs, c.projectConfig.ReportConfigs...)
	}

	return s
}
