// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a program that reads cobalt configuration in a YAML format
// and outputs it as a CobaltConfig serialized protocol buffer.

package main

import (
	"config"
	"config_parser"
	"flag"
	"github.com/golang/glog"
	"github.com/golang/protobuf/proto"
	"io/ioutil"
	"os"
)

var (
	repoUrl   = flag.String("repo_url", "", "URL of the repository containing the config. Exactly one of 'repo_url' or 'config_dir' must be specified.")
	configDir = flag.String("config_dir", "", "Directory containing the config. Exactly one of 'repo_url' or 'config_dir' must be specified.")
	outFile   = flag.String("output_file", "", "File to which the serialized config should be written. Defaults to stdout.")
	checkOnly = flag.Bool("check_only", false, "Only check that the configuration is valid.")
)

func main() {
	flag.Parse()

	if (*repoUrl == "") == (*configDir == "") {
		glog.Exit("Exactly one of 'repo_url' and 'config_dir' must be set.")
	}

	if *outFile != "" && *checkOnly {
		glog.Exit("'output_file' does not make sense if 'check_only' is set.")
	}

	// First, we parse the configuration from the specified location.
	var c config.CobaltConfig
	var err error
	if *repoUrl != "" {
		c, err = config_parser.ReadConfigFromRepo(*repoUrl)
	} else {
		c, err = config_parser.ReadConfigFromDir(*configDir)
	}

	if err != nil {
		glog.Exit(err)
	}

	// Then, we serialize the configuration.
	configBytes, err := proto.Marshal(&c)
	if err != nil {
		glog.Exit(err)
	}

	// If no errors have occured yet and checkOnly was set, we are done.
	if *checkOnly {
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
			glog.Exit(err)
		}
	}

	os.Exit(0)
}
