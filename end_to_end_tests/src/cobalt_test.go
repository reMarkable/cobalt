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
	"google.golang.org/api/option"
	"google.golang.org/grpc"
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
	// These strings must coincide with the ones in bigtable_emulator_helper.h
	f.project = "TestProject"
	f.instance = "TestInstance"
	// This name must coincide with the one in bigtable_names.h
	f.table = "observations"

	sysRootDir, _ := filepath.Abs(filepath.Join(filepath.Dir(os.Args[0]), "../../sysroot"))
	bin := filepath.Join(sysRootDir, "gcloud", "google-cloud-sdk", "platform", "bigtable-emulator", "cbtemulator")
	f.cmd = exec.Command(bin)

	stdout, _ := f.cmd.StdoutPipe()
	reader := bufio.NewReader(stdout)

	// Create a process group so we can kill children
	f.cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	log.Printf("Starting Bigtable Emulator: %v", bin)
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
	log.Printf("Bigtable Emulator started with host: %v", f.host)

	return f, nil
}

func (f *BigtableFixture) Close() {
	// Kill process group
	pgid, _ := syscall.Getpgid(f.cmd.Process.Pid)
	syscall.Kill(-pgid, syscall.SIGTERM)

	f.cmd.Wait()
}

func (f *BigtableFixture) CountRows() int {
	conn, err := grpc.Dial(f.host, grpc.WithInsecure())
	if err != nil {
		log.Printf("Error while attempting to connect to the Bigtable Emulator: %v", err)
		return -1
	}

	ctx := context.Background()
	client, err := bigtable.NewClient(ctx, f.project, f.instance, option.WithGRPCConn(conn))
	if err != nil {
		log.Printf("Error while attempting to create a Bigtable Client: %v", err)
		return -1
	}
	defer client.Close()
	tbl := client.Open(f.table)

	count := 0
	err = tbl.ReadRows(ctx, bigtable.InfiniteRange(""),
		func(row bigtable.Row) bool {
			count++
			return true
		})
	if err != nil {
		log.Printf("Error while attempting to count rows in Bigtable: %v", err)
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

	f.outdir, _ = filepath.Abs(filepath.Join(filepath.Dir(os.Args[0]), "../../out"))
	abin := filepath.Join(f.outdir, "analyzer", "analyzer")

	configDir, _ := filepath.Abs(filepath.Join(f.outdir, "..", "config", "registered"))
	f.cmd = exec.Command(abin, "--port=8080", "-for_testing_only_use_bigtable_emulator",
		"--cobalt_config_dir", configDir, "-logtostderr")
	var out bytes.Buffer
	f.cmd.Stdout = &out
	var serr bytes.Buffer
	f.cmd.Stderr = &serr

	log.Printf("Starting Analyzer: %v", strings.Join(f.cmd.Args, " "))
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

	bin, _ := filepath.Abs(filepath.Join(f.analyzer.outdir, "shuffler", "shuffler"))

	shufflerTestConfig, _ := filepath.Abs(filepath.Join(f.outdir, "config", "shuffler_default.conf"))
	f.cmd = exec.Command(bin,
		"-config_file", shufflerTestConfig,
		"-batch_size", strconv.Itoa(100),
		"-vmodule=receiver=2,dispatcher=2,store=2",
		"-logtostderr")

	var out bytes.Buffer
	f.cmd.Stdout = &out
	var serr bytes.Buffer
	f.cmd.Stderr = &serr

	log.Printf("Starting Shuffler: %v", strings.Join(f.cmd.Args, " "))
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
		"-analyzer_uri", "localhost:8080",
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
		"-shuffler_uri", "localhost:50051",
		"-analyzer_uri", "localhost:8080",
		"-num_observations", strconv.Itoa(num),
		"-num_rpcs", strconv.Itoa(num))

	log.Printf("Running cgen: %v", strings.Join(cmd.Args, " "))
	if cmd.Run() != nil {
		t.Error("cgen failed")
		return
	}
	log.Printf("cgen completed")

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
