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
	"bytes"
	"errors"
	"fmt"
	"log"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"testing"
	"time"

	"cloud.google.com/go/bigtable"
	"golang.org/x/net/context"
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

func (f *BigtableFixture) CountRows() int {
	ctx := context.Background()
	client, _ := bigtable.NewClient(ctx, f.project, f.instance)
	defer client.Close()
	tbl := client.Open(f.table)

	count := 0
	err := tbl.ReadRows(ctx, bigtable.InfiniteRange(""),
		func(row bigtable.Row) bool {
			count++
			return true
		})
	if err != nil {
		return -1
	}

	return count
}

// This fixture stands up the Analyzer.  It depends on Bigtable being up.
type AnalyzerFixture struct {
	cmd      *exec.Cmd
	cgen     string
	outdir   string
	bigtable *BigtableFixture
}

func DaemonStarted(dst string) bool {
	for i := 0; i < 10; i++ {
		conn, err := net.Dial("tcp", dst)
		if err == nil {
			conn.Close()
			return true
		}
		time.Sleep(10 * time.Millisecond)
	}

	return false
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

	f.outdir = filepath.Join(filepath.Dir(os.Args[0]), "../../out")
	abin := filepath.Join(f.outdir, "/analyzer/analyzer")

	table := fmt.Sprintf("projects/%v/instances/%v/tables/%v",
		bigtable.project, bigtable.instance, bigtable.table)
	f.cmd = exec.Command(abin, "-table", table)
	var out bytes.Buffer
	f.cmd.Stdout = &out
	var serr bytes.Buffer
	f.cmd.Stderr = &serr

	os.Setenv("BIGTABLE_EMULATOR_HOST", bigtable.host)

	log.Printf("Starting Analyzer...")
	err = f.cmd.Start()
	if err != nil {
		log.Printf("Command finished with error:[%v] with stdout:[%s] and stderr:[%s]", err, out.String(), serr.String())
		log.Fatal(err)
	}

	// Wait for it to start
	if !DaemonStarted("127.0.0.1:8080") {
		f.Close()
		return nil, errors.New("Unable to start the analyzer")
	}

	// get path to cgen
	f.cgen = filepath.Join(f.outdir, "/tools/cgen")

	return f, nil
}

func (f *AnalyzerFixture) Close() {
	f.cmd.Process.Kill()
	f.cmd.Wait()
	f.bigtable.Close()
}

// Fixture to start the Shuffler.  It depends on the Analyzer fixture, which in
// turn depends on the Bigtable fixture.  The Shuffler fixture will start the
// entire backend system.
type ShufflerFixture struct {
	cmd      *exec.Cmd
	analyzer *AnalyzerFixture
	outdir   string
}

func NewShufflerFixture() (*ShufflerFixture, error) {
	// Create Analyzer first
	analyzer, err := NewAnalyzerFixture()
	if err != nil {
		return nil, err
	}

	// Create the Shuffler
	f := new(ShufflerFixture)
	f.analyzer = analyzer
	f.outdir = filepath.Join(filepath.Dir(os.Args[0]), "../../out")

	bin := filepath.Join(f.analyzer.outdir, "shuffler/shuffler")

	shufflerTestConfig := filepath.Join(f.outdir, "config/shuffler_default.conf")
	f.cmd = exec.Command(bin,
		"-config_file", shufflerTestConfig,
		"-batch_size", strconv.Itoa(100),
		"-v", strconv.Itoa(2),
		"-vmodule=receiver=2,dispatcher=2,store=2",
		"-logtostderr")

	var out bytes.Buffer
	f.cmd.Stdout = &out
	var serr bytes.Buffer
	f.cmd.Stderr = &serr

	log.Printf("Starting Shuffler...")
	err = f.cmd.Start()
	if err != nil {
		log.Printf("Command finished with error:[%v] with stdout:[%s] and stderr:[%s]", err, out.String(), serr.String())
		log.Fatal(err)
	}

	if !DaemonStarted("127.0.0.1:50051") {
		f.Close()
		return nil, errors.New("Unable to start the shuffler")
	}

	return f, nil
}

func (f *ShufflerFixture) Close() {
	f.cmd.Process.Kill()
	f.cmd.Wait()
	f.analyzer.Close()
}

// This test depends on the AnalyzerFixture.
//
// It uses cgen to create 2 fake reports.
// It then asserts that 2 reports exist in Bigtable.
func OTestAnalyzerAddObservations(t *testing.T) {
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
	count := f.bigtable.CountRows()
	if count == -1 {
		t.Error("Can't read rows")
		return
	}

	if count != num {
		t.Errorf("Unexpected number of rows got %v want %v", count, num)
		return
	}
}

// This test depends on the ShufflerFixture.
//
// It uses cgen to create 2 fake reports.
// It then asserts that 2 reports exist in Bigtable.
func TestShufflerProcess(t *testing.T) {
	// Start the entire system.
	f, err := NewShufflerFixture()
	if err != nil {
		t.Error("Fixture failed:", err)
		return
	}
	defer f.Close()

	// Run cgen on the analyzer to create 2 observations
	num := 2
	cmd := exec.Command(f.analyzer.cgen,
		"-shuffler", "127.0.0.1",
		"-analyzer", "127.0.0.1",
		"-num_observations", strconv.Itoa(num),
		"-num_rpcs", strconv.Itoa(num))
	if cmd.Run() != nil {
		t.Error("cgen failed")
		return
	}

	var rows int

	// The shuffler RPC is async so it could take a while before the data
	// reaches bigtable.  Try multiple times.
	for i := 0; i < 5; i++ {
		rows = f.analyzer.bigtable.CountRows()
		if rows == -1 {
			t.Error("Can't read rows")
			return
		}

		if rows == num {
			break
		}

		time.Sleep(10 * time.Millisecond)
	}

	if rows != num {
		t.Errorf("Unexpected number of rows got %v want %v", rows, num)
		return
	}
}
