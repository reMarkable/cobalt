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

package shuffler_config

import (
	"bufio"
	"errors"
	"fmt"
	"io/ioutil"
	"os"

	"github.com/golang/glog"
	"github.com/golang/protobuf/proto"

	"shuffler"
)

// LoadConfig reads Shuffler configuration params from a text file
// |configFileName|, deserializes them to |ShufflerConfig| proto and returns it.
func LoadConfig(configFileName string) (*shuffler.ShufflerConfig, error) {
	if configFileName == "" {
		return nil, errors.New("Provide a valid Shuffler config file")
	}

	// detect if file exists
	glog.Info("Will read Shuffler configuration from ", configFileName, ".")
	var _, err = os.Stat(configFileName)
	if err != nil {
		return nil, err
	}
	config := &shuffler.ShufflerConfig{}
	serializedBytes, err := ioutil.ReadFile(configFileName)
	if err != nil {
		return config, err
	}
	err = proto.UnmarshalText(string(serializedBytes), config)
	if err == nil {
		glog.Info("Successfully read the following configuration: ", toString(config))
	}
	return config, err
}

func toString(config *shuffler.ShufflerConfig) string {
	return fmt.Sprintf("{FrequenceInHours:%d, Threshold:%d, DisposalAgeDays:%d}",
		config.GlobalConfig.FrequencyInHours,
		config.GlobalConfig.Threshold,
		config.GlobalConfig.DisposalAgeDays)
}

// WriteConfig serializes the input Shuffler configuration params to a
// output text file |configFileName|.
func WriteConfig(config *shuffler.ShufflerConfig, configFileName string) error {
	if config == nil {
		// Provide default config.
		config = &shuffler.ShufflerConfig{}
		config.GlobalConfig = &shuffler.Policy{
			FrequencyInHours: 24,
			PObservationDrop: 0.0,
			Threshold:        10,
			AnalyzerUrl:      "localhost",
			DisposalAgeDays:  4,
		}
	}

	if configFileName == "" {
		return errors.New("Provide a valid Shuffler config file")
	}

	f, err := os.Create(configFileName)
	if err != nil {
		return err
	}

	writer := bufio.NewWriter(f)
	_, err = writer.WriteString(config.String())
	writer.Flush()

	return err
}
