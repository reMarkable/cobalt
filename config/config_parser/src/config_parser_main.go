// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a program that reads cobalt configuration in a YAML format
// and outputs it as a CobaltConfig serialized protocol buffer.

package main

import (
	"config"
	"config_parser"
	"config_validator"
	"flag"
	"fmt"
	"github.com/golang/glog"
	"io"
	"io/ioutil"
	"os"
	"strings"
	"time"
)

var (
	repoUrl        = flag.String("repo_url", "", "URL of the repository containing the config. Exactly one of 'repo_url', 'config_file' or 'config_dir' must be specified.")
	configDir      = flag.String("config_dir", "", "Directory containing the config. Exactly one of 'repo_url', 'config_file' or 'config_dir' must be specified.")
	configFile     = flag.String("config_file", "", "File containing the config for a single project. Exactly one of 'repo_url', 'config_file' or 'config_dir' must be specified.")
	outFile        = flag.String("output_file", "", "File to which the serialized config should be written. Defaults to stdout.")
	checkOnly      = flag.Bool("check_only", false, "Only check that the configuration is valid.")
	skipValidation = flag.Bool("skip_validation", false, "Skip validating the config, write it no matter what.")
	gitTimeoutSec  = flag.Int64("git_timeout", 60, "How many seconds should I wait on git commands?")
	customerId     = flag.Int64("customer_id", -1, "Customer Id for the config to be read. Must be set if and only if 'config_file' is set.")
	projectId      = flag.Int64("project_id", -1, "Project Id for the config to be read. Must be set if and only if 'config_file' is set.")
	outFormat      = flag.String("out_format", "bin", "Specifies the output format. Supports 'bin' (serialized proto), 'b64' (serialized proto to base 64) and 'cpp' (ta C++ file containing a variable with a base64-encoded serialized proto.)")
	varName        = flag.String("var_name", "config", "When using the 'cpp' output format, this will specify the variable name to be used in the output.")
	namespace      = flag.String("namespace", "", "When using the 'cpp' output format, this will specify the comma-separated namespace within which the config variable must be places.")
	genDepFile     = flag.Bool("gen_dep_file", false, "Generate a depfile (see gn documentation) that lists all the project configuration files. This should be used in conjunction with the 'config_dir' and 'output_file' flags only.")
)

// Write a depfile listing the files in 'files' at the location specified by
// outFile.
func writeDepFile(files []string, outFile string) error {
	w, err := os.OpenFile(outFile, os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return err
	}
	defer w.Close()

	_, err = io.WriteString(w, fmt.Sprintf("%s: %s", outFile, strings.Join(files, " ")))
	return err
}

func main() {
	flag.Parse()

	if (*repoUrl == "") == (*configDir == "") == (*configFile == "") {
		glog.Exit("Exactly one of 'repo_url', 'config_file' and 'config_dir' must be set.")
	}

	if *configFile == "" && (*customerId >= 0 || *projectId >= 0) {
		glog.Exit("'customer_id' and 'project_id' must be set if and only if 'config_file' is set.")
	}

	if *configFile != "" && (*customerId < 0 || *projectId < 0) {
		glog.Exit("If 'config_file' is set, both 'customer_id' and 'project_id' must be set.")
	}

	if *outFile != "" && *checkOnly {
		glog.Exit("'output_file' does not make sense if 'check_only' is set.")
	}

	if *genDepFile && *configDir == "" {
		glog.Exit("'gen_dep_file' is only compatible with 'config_dir' being set.")
	}

	if *genDepFile && *outFile == "" {
		glog.Exit("'gen_dep_file' requires that 'output_file' be set.")
	}

	var configLocation string
	if *repoUrl != "" {
		configLocation = *repoUrl
	} else if *configFile != "" {
		configLocation = *configFile
	} else {
		configLocation = *configDir
	}

	if *genDepFile {
		files, err := config_parser.GetConfigFilesListFromConfigDir(configLocation)
		if err != nil {
			glog.Exit(err)
		}

		if err := writeDepFile(files, *outFile); err != nil {
			glog.Exit(err)
		}

		os.Exit(0)
	}

	var outputFormatter config_parser.OutputFormatter
	switch *outFormat {
	case "bin":
		outputFormatter = config_parser.BinaryOutput
	case "b64":
		outputFormatter = config_parser.Base64Output
	case "cpp":
		namespaceList := []string{}
		if *namespace != "" {
			namespaceList = strings.Split(*namespace, ",")
		}
		outputFormatter = config_parser.CppOutputFactory(*varName, namespaceList, configLocation)
	default:
		glog.Exitf("'%v' is an invalid out_format parameter. 'bin', 'b64' and 'cpp' are the only valid values for out_format.", *outFormat)
	}

	// First, we parse the configuration from the specified location.
	var c config.CobaltConfig
	var err error
	if *repoUrl != "" {
		gitTimeout := time.Duration(*gitTimeoutSec) * time.Second
		c, err = config_parser.ReadConfigFromRepo(*repoUrl, gitTimeout)
	} else if *configFile != "" {
		c, err = config_parser.ReadConfigFromYaml(*configFile, uint32(*customerId), uint32(*projectId))
	} else {
		c, err = config_parser.ReadConfigFromDir(*configDir)
	}

	if err != nil {
		glog.Exit(err)
	}

	if !*skipValidation {
		if err = config_validator.ValidateConfig(&c); err != nil {
			glog.Exit(err)
		}
	}

	// Then, we serialize the configuration.
	configBytes, err := outputFormatter(&c)
	if err != nil {
		glog.Exit(err)
	}

	// Check that the output file is not empty.
	if len(configBytes) == 0 {
		glog.Exit("Output file is empty.")
	}

	// If no errors have occured yet and checkOnly was set, we are done.
	if *checkOnly {
		fmt.Printf("%s OK\n", configLocation)
		os.Exit(0)
	}

	// By default we print the output to stdout.
	w := os.Stdout

	// If an output file is specified, we write to a temporary file and then rename
	// the temporary file with the specified output file name.
	if *outFile != "" {
		if w, err = ioutil.TempFile("", "cobalt_config"); err != nil {
			glog.Exit(err)
		}
		defer w.Close()
	}

	_, err = w.Write(configBytes)
	if err != nil {
		glog.Exit(err)
	}

	if *outFile != "" {
		if err := os.Rename(w.Name(), *outFile); err != nil {
			// Rename doesn't work if /tmp is in a different partition. Attempting to copy.
			// TODO(azani): Look into doing this atomically.
			in, err := os.Open(w.Name())
			if err != nil {
				glog.Exit(err)
			}
			defer in.Close()

			out, err := os.Create(*outFile)
			if err != nil {
				glog.Exit(err)
			}
			defer out.Close()

			_, err = io.Copy(out, in)
			if err != nil {
				glog.Exit(err)
			}
		}
	}

	os.Exit(0)
}
