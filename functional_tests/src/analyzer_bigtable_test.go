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

// This test stands up the Bigtable emulator, the analzyer, and uses cgen to
// generate fake reports.  It then verifies the presence of the reports in
// Bigtable.

package main

import (
	"bufio"
	"cloud.google.com/go/bigtable"
	"errors"
	"fmt"
	"golang.org/x/net/context"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"testing"
	"time"
)

// This fixture stands up the bigtable emulator.
type BigtableFixture struct {
	cmd      *exec.Cmd
	host     string
	project  string
	instance string
	table    string
}

func NewBigtableFixture() (*BigtableFixture, error) {
	f := new(BigtableFixture)

	f.project = "google.com:shuffler-test"
	f.instance = "cobalt-analyzer"
	f.table = "observations"

	f.cmd = exec.Command("gcloud", "beta", "emulators", "bigtable", "start")

	stderr, _ := f.cmd.StderrPipe()
	reader := bufio.NewReader(stderr)

	// Create a process group so we can kill children
	f.cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	f.cmd.Start()

	// Wait for bigtable to start
	for {
		line, err := reader.ReadString('\n')
		if err != nil {
			return nil, err
		}

		if strings.Contains(line, "running") {
			fields := strings.Fields(line)
			f.host = fields[len(fields)-1]
			break
		}
	}

	return f, nil
}

func (f *BigtableFixture) Close() {
	// Kill process group
	pgid, _ := syscall.Getpgid(f.cmd.Process.Pid)
	syscall.Kill(-pgid, syscall.SIGTERM)

	f.cmd.Wait()
}

// This fixture stands up the Analyzer.  It depends on Bigtable being up.
type AnalyzerFixture struct {
	cmd      *exec.Cmd
	cgen     string
	bigtable *BigtableFixture
}

func NewAnalyzerFixture() (*AnalyzerFixture, error) {
	// Create Bigtable first
	bigtable, err := NewBigtableFixture()
	if err != nil {
		return nil, err
	}

	// Create the Analyzer
	f := new(AnalyzerFixture)
	f.bigtable = bigtable

	out := filepath.Join(filepath.Dir(os.Args[0]), "../../out")
	abin := filepath.Join(out, "/analyzer/analyzer")

	table := fmt.Sprintf("projects/%v/instances/%v/tables/%v",
		bigtable.project, bigtable.instance, bigtable.table)
	f.cmd = exec.Command(abin, table)

	os.Setenv("BIGTABLE_EMULATOR_HOST", bigtable.host)

	f.cmd.Start()

	// Wait for it to start
	success := false
	for i := 0; i < 10; i++ {
		conn, err := net.Dial("tcp", "127.0.0.1:8080")
		if err == nil {
			success = true
			conn.Close()
			break
		}
		time.Sleep(10 * time.Millisecond)
	}

	if !success {
		f.Close()
		return nil, errors.New("Can't connect")
	}

	// get path to cgen
	f.cgen = filepath.Join(out, "/tools/cgen")

	return f, nil
}

func (f *AnalyzerFixture) Close() {
	f.cmd.Process.Kill()
	f.cmd.Wait()
	f.bigtable.Close()
}

// This test depends on the AnalyzerFixture.
//
// It uses cgen to create 2 fake reports.
// It then asserts that 2 reports exist in Bigtable.
func TestAnalyzerAddObservations(t *testing.T) {
	// Start the Analyzer and Bigtable emulator
	f, err := NewAnalyzerFixture()
	if err != nil {
		t.Error("Fixture failed:", err)
		return
	}
	defer f.Close()

	// Run cgen on the analyzer to create 2 observations
	num := 2
	cmd := exec.Command(f.cgen,
		"-analyzer", "127.0.0.1",
		"-num_observations", strconv.Itoa(num))
	if cmd.Run() != nil {
		t.Error("cgen failed")
		return
	}

	// Grab the observations from bigtable
	ctx := context.Background()
	client, _ := bigtable.NewClient(ctx, f.bigtable.project, f.bigtable.instance)
	defer client.Close()
	tbl := client.Open(f.bigtable.table)

	count := 0
	err = tbl.ReadRows(ctx, bigtable.InfiniteRange(""), func(row bigtable.Row) bool {
		count++
		return true
	})
	if err != nil {
		t.Error("Can't read rows %v", err)
		return
	}

	if count != num {
		t.Error("Unexpected number of rows")
		return
	}
}
