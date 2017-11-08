// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements reading the Cobalt configuration from a git repository.

package config_parser

import (
	"config"
	"fmt"
	"io/ioutil"
	"net/url"
	"os"
	"os/exec"
)

// Clones the specified repository to the specified destination.
func cloneRepo(repoUrl string, destination string) error {
	cmd := exec.Command("git",
		// Truncate the history to the latest commit.
		"--depth", "1",
		repoUrl, destination)
	// *exec.ExitError is the documented return type of Cmd.Run().
	if err := cmd.Run(); err != nil {
		return err
	}

	return nil
}

// Only allow URLs served over HTTPS.
func checkUrl(repoUrl string) (err error) {
	u, err := url.Parse(repoUrl)
	if err != nil {
		return err
	}

	if u.Scheme != "https" {
		return fmt.Errorf("For security reasons, we only accept repositories served over https.")
	}

	return nil
}

// ReadConfigFromRepo clones repoUrl into a temporary directory and reads the
// configuration from it. For the organization expected of the repository, see
// ReadConfigFromDir in config_reader.go.
func ReadConfigFromRepo(repoUrl string) (c config.CobaltConfig, err error) {
	if err = checkUrl(repoUrl); err != nil {
		return c, err
	}

	repoPath, err := ioutil.TempDir(os.TempDir(), "cobalt_config")
	if err != nil {
		return c, err
	}

	defer os.RemoveAll(repoPath)

	if err := cloneRepo(repoUrl, repoPath); err != nil {
		return c, fmt.Errorf("Error cloning repository (%v): %v", repoUrl, err)
	}

	return ReadConfigFromDir(repoPath)
}
