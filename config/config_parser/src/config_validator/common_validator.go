// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config_validator

import (
	"bytes"
	"config"
	"flag"
	"fmt"
	"os"
	"os/exec"

	"github.com/golang/protobuf/proto"
)

var (
	configValidatorBin = flag.String("config_validator_bin", "", "The location of the config_validator binary. Must be specified.")
)

// runCommonValidations runs the config_validator_bin, writes the marshaled
// CobaltConfig to stdin, and reads the error message from stdout. If the error
// message is "", then we consider that no error.
func runCommonValidations(config *config.CobaltConfig) (err error) {
	data, err := proto.Marshal(config)
	if err != nil {
		return err
	}

	if *configValidatorBin == "" {
		return fmt.Errorf("Failed common validation. No validator binary supplied")
	}
	outb := new(bytes.Buffer)
	validator := exec.Command(*configValidatorBin)
	validator.Stderr = os.Stderr
	validator.Stdout = outb
	stdin, err := validator.StdinPipe()
	if err != nil {
		return fmt.Errorf("Unable to get stdinPipe: %v", err)
	}
	err = validator.Start()
	if err != nil {
		return fmt.Errorf("Failed to start command: %v", err)
	}
	bytesWritten, err := stdin.Write(data)
	if err != nil {
		return fmt.Errorf("unable to write data to stdin: %v", err)
	}
	if bytesWritten < len(data) {
		return fmt.Errorf("Could not write all of the data to the subprocess")
	}
	stdin.Close()

	if err := validator.Wait(); err != nil {
		return fmt.Errorf("Waiting for execution failed: %v", err)
	}

	responseStr := outb.String()
	if responseStr != "" {
		return fmt.Errorf(responseStr)
	}

	return nil
}
