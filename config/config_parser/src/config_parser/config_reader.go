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
	"reflect"
	"sort"
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

// ReadConfigFromYaml reads the configuration for a single project from a single yaml file.
// See project_config.go for the format.
func ReadConfigFromYaml(yamlConfigPath string, customerId uint32, projectId uint32) (c config.CobaltConfig, err error) {
	yamlConfig, err := ioutil.ReadFile(yamlConfigPath)
	if err != nil {
		return c, err
	}

	p := projectConfig{}
	p.customerId = customerId
	p.projectId = projectId
	if err := parseProjectConfig(string(yamlConfig), &p); err != nil {
		return c, err
	}

	c.EncodingConfigs = p.projectConfig.EncodingConfigs
	c.MetricConfigs = p.projectConfig.MetricConfigs
	c.ReportConfigs = p.projectConfig.ReportConfigs

	return c, nil
}

// GetConfigFilesListFromConfigDir reads the configuration for Cobalt from a
// directory on the file system (See ReadConfigFromDir) and returns the list
// of files which constitute the configuration. The purpose is generating a
// list of dependencies.
func GetConfigFilesListFromConfigDir(rootDir string) (files []string, err error) {
	r, err := newConfigDirReader(rootDir)
	if err != nil {
		return files, err
	}

	l := []projectConfig{}
	if err := readProjectsList(r, &l); err != nil {
		return files, err
	}

	files = append(files, r.customersFilePath())

	for i, _ := range l {
		c := &(l[i])
		files = append(files, r.projectFilePath(c.customerName, c.projectName))
	}
	return files, nil
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

// newConfigDirReader returns a pointer to a configReader which will read the
// Cobalt configuration stored in the provided directory.
func newConfigDirReader(configDir string) (r *configDirReader, err error) {
	info, err := os.Stat(configDir)
	if err != nil {
		return nil, err
	}

	if !info.IsDir() {
		return nil, fmt.Errorf("%v is not a directory.", configDir)
	}

	return &configDirReader{configDir: configDir}, nil
}

// newConfigReaderForDir returns a configReader which will read the Cobalt
// configuration stored in the provided directory.
func newConfigReaderForDir(configDir string) (r configReader, err error) {
	return newConfigDirReader(configDir)
}

func (r *configDirReader) customersFilePath() string {
	// The customer and project list is at <rootDir>/projects.yaml
	return filepath.Join(r.configDir, "projects.yaml")
}

func (r *configDirReader) Customers() (string, error) {
	customerList, err := ioutil.ReadFile(r.customersFilePath())
	if err != nil {
		return "", err
	}
	return string(customerList), nil
}

func (r *configDirReader) projectFilePath(customerName string, projectName string) string {
	// A project's config is at <rootDir>/<customerName>/<projectName>/config.yaml
	return filepath.Join(r.configDir, customerName, projectName, "config.yaml")
}

func (r *configDirReader) Project(customerName string, projectName string) (string, error) {
	projectConfig, err := ioutil.ReadFile(r.projectFilePath(customerName, projectName))
	if err != nil {
		return "", err
	}
	return string(projectConfig), nil
}

func readProjectsList(r configReader, l *[]projectConfig) (err error) {
	// First, we get and parse the customer list.
	customerListYaml, err := r.Customers()
	if err != nil {
		return err
	}

	if err = parseCustomerList(customerListYaml, l); err != nil {
		return err
	}

	return nil
}

// readConfig reads and parses the configuration for all projects from a configReader.
func readConfig(r configReader, l *[]projectConfig) (err error) {
	if err = readProjectsList(r, l); err != nil {
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

// cmpConfigEntry takes two protobuf pointers that must have the fields
// "CustomerId", "ProjectId", and "Id". It is used in generically sorting the
// config entries in the CobaltConfig proto.
func cmpConfigEntry(i, j interface{}) bool {
	a := reflect.ValueOf(i).Elem()
	b := reflect.ValueOf(j).Elem()

	aCi := a.FieldByName("CustomerId").Uint()
	bCi := b.FieldByName("CustomerId").Uint()
	if aCi != bCi {
		return aCi < bCi
	}

	aPi := a.FieldByName("ProjectId").Uint()
	bPi := b.FieldByName("ProjectId").Uint()
	if aPi != bPi {
		return aPi < bPi
	}

	ai := a.FieldByName("Id").Uint()
	bi := b.FieldByName("Id").Uint()
	if ai != bi {
		return ai < bi
	}

	return false
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

	// In order to ensure that we output a stable order in the binary protobuf, we
	// sort each slice of config entries.
	sort.SliceStable(s.EncodingConfigs, func(i, j int) bool {
		return cmpConfigEntry(s.EncodingConfigs[i], s.EncodingConfigs[j])
	})
	sort.SliceStable(s.MetricConfigs, func(i, j int) bool {
		return cmpConfigEntry(s.MetricConfigs[i], s.MetricConfigs[j])
	})
	sort.SliceStable(s.ReportConfigs, func(i, j int) bool {
		return cmpConfigEntry(s.ReportConfigs[i], s.ReportConfigs[j])
	})

	return s
}
