// Copyright 2016 The Fuchsia Authors
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

package config

import (
	"os"
	"path/filepath"
	"reflect"
	"strconv"
	"strings"
	"testing"
	"time"

	shufflerpb "cobalt"
)

var configDir = "shuffler/src/config"

func getTmpFile() string {
	tmpFileSuffix := uint32(time.Now().UnixNano() + int64(os.Getpid()))
	return strings.Join([]string{"/tmp/cobalt_sconf_", strconv.Itoa(int(tmpFileSuffix)), ".txt"}, "")
}

// TestDefaultConfig validates generation and loading of a config file with
// default configuration parameters.
func TestDefaultConfig(t *testing.T) {
	configFileName := getTmpFile()

	// Generate default config
	err := WriteConfig(nil, configFileName)
	if err != nil {
		t.Errorf("Error in generating valid config file: %v", err)
	}

	got, err := LoadConfig(configFileName)
	if err != nil {
		t.Errorf("Error loading the config file: %v", err)
	}

	want := &shufflerpb.ShufflerConfig{}
	want.GlobalConfig = &shufflerpb.Policy{
		FrequencyInHours: 24,
		PObservationDrop: 0.0,
		Threshold:        10,
		AnalyzerUrl:      "localhost",
		DisposalAgeDays:  4,
	}

	if !reflect.DeepEqual(got, want) {
		t.Errorf("Got response: %v, expecting: %v", got, want)
	}

	err = os.Remove(configFileName)
	if err != nil {
		t.Errorf("Error deleting the temp config file: %v", err)
	}
}

// TestCustomConfig validates generation and loading of a config file with
// custom configuration parameters.
func TestCustomConfig(t *testing.T) {
	configFileName := getTmpFile()

	in := &shufflerpb.ShufflerConfig{}
	in.GlobalConfig = &shufflerpb.Policy{
		FrequencyInHours: 2,
		PObservationDrop: 0.5,
		Threshold:        200,
		AnalyzerUrl:      "www.google.com",
		DisposalAgeDays:  6,
	}

	// Generate custom config
	err := WriteConfig(in, configFileName)
	if err != nil {
		t.Errorf("Error in generating valid config file: %v", err)
	}

	out, err := LoadConfig(configFileName)
	if err != nil {
		t.Errorf("Error loading the config file: %v", err)
	}

	if !reflect.DeepEqual(out, in) {
		t.Errorf("Got response: %v, expecting: %v", out, in)
	}

	err = os.Remove(configFileName)
	if err != nil {
		t.Errorf("Error deleting the temp config file: %v", err)
	}
}

// TestLoadConfigWithRegisteredConfig validates the format of the registered
// config file that is checked in. It does not validate the contents of the
// config file.
func TestLoadConfigWithRegisteredConfig(t *testing.T) {
	pwd, err := os.Getwd()
	if err != nil {
		t.Errorf("Error retrieving the current working dir: %v", err)
	}
	configFileName := filepath.Join(pwd, configDir, "config_v0.txt")

	_, err = LoadConfig(configFileName)
	if err != nil {
		t.Errorf("Error loading the config file: %v", err)
	}
}

// TestLoadConfigWithValidTestConfig validates both the format and the contents
// of the test config file.
func TestLoadConfigWithValidTestConfig(t *testing.T) {
	pwd, err := os.Getwd()
	if err != nil {
		t.Errorf("Error retrieving the current working dir: %v", err)
	}
	configFileName := filepath.Join(pwd, configDir, "testdata", "config_valid.txt")

	config, err := LoadConfig(configFileName)
	if err != nil {
		t.Errorf("Error loading the config file: %v", err)
	}

	if config.GetGlobalConfig().FrequencyInHours != 24 ||
		config.GetGlobalConfig().Threshold != 10 ||
		config.GetGlobalConfig().AnalyzerUrl != "localhost" ||
		config.GetGlobalConfig().DisposalAgeDays != 4 {
		t.Errorf("Got %v, expecting valid config.", config)
	}
}

// TestLoadConfigWithInvalidTestConfig validates that an invalid config file cannot
// be loaded.
func TestLoadConfigWithInvalidTestConfig(t *testing.T) {
	pwd, err := os.Getwd()
	if err != nil {
		t.Errorf("Error retrieving the current working dir: %v", err)
	}
	configFileName := filepath.Join(pwd, configDir, "testdata", "config_invalid.txt")

	_, err = LoadConfig(configFileName)
	if err == nil {
		t.Errorf("Error expected for invalid config data.")
	}
}
