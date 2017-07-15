// Copyright 2017 The Fuchsia Authors
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

/*
This file contains a program that runs and prints Cobalt reports.
The program may be used in two modes, specified by the boolean flag -interactive.

In interactive mode the program runs an interactive command loop that
allows an operator to run multiple reports, specifying the ReportConfig id
for each report.

In non-interactive mode the program runs a single report using the
ReportConfig id specified by the flag -report_config_id.

In both cases the customer and project IDs are specified via the flags
-customer_id and -project_id and the output of the report is written to
CSV format to the console, or to the file specified by the flag -csv_file.
*/

package main

import (
	"bufio"
	"bytes"
	"flag"
	"fmt"
	"io/ioutil"
	"math"
	"os"
	"strconv"
	"strings"
	"time"

	"analyzer/report_master"
	"report_client"
)

var (
	tls    = flag.Bool("tls", false, "Connection uses TLS if true, else plain TCP")
	caFile = flag.String("ca_file", "", "The file containning the root CA certificate.")

	reportMasterURI = flag.String("report_master_uri", "", "The URI of the ReportMaster Service")

	customerID     = flag.Uint("customer_id", 1, "The Cobalt customer ID.")
	projectID      = flag.Uint("project_id", 1, "The Cobalt project ID.")
	reportConfigID = flag.Uint("report_config_id", 1, "The ReportConfig ID. Used in non-interactive mode only.")
	firstDay       = flag.Int64("first_day", math.MaxInt64, "If -first_day and -last_day are specified they should be (usually negative) "+
		"offsets relative to today specifying a range of days over which the report should be run. Otherwise the range is unbounded.")
	lastDay = flag.Int64("last_day", math.MaxInt64, "If -first_day and -last_day are specified they should be (usually negative) "+
		"offsets relative to today specifying a range of days over which the report should be run. Otherwise the range is unbounded.")

	interactive = flag.Bool("interactive", true, "If false then exuecute the command specified by the flags and exit.  "+
		"Don't enter a command loop.")

	includeStdErrColumn = flag.Bool("include_std_err_column", false, "Should a standard error column be included in the report? "+
		"Used in non-interactive mode only.")

	csvFile = flag.String("csv_file", "", "If specified then the CSV report will be written to that file."+
		"Used in non-interactive mode only.")

	deadlineSeconds = flag.Uint("deadline_seconds", 30, "Number of seconds to wait for a report to complete before failing.")
)

type ReportClientCLI struct {
	report       *report_master.Report
	reportClient *report_client.ReportClient
}

func (c *ReportClientCLI) PrintCSVReport(includeStdErr bool) error {
	var buffer bytes.Buffer
	err := report_client.WriteCSVReport(&buffer, c.report, includeStdErr)
	if err != nil {
		return err
	}
	fmt.Println(buffer.String())
	if csvFile != nil && len(*csvFile) > 0 {
		fmt.Printf("Writing CSV to file %s.\n", *csvFile)
		return ioutil.WriteFile(*csvFile, buffer.Bytes(), os.ModePerm)
	}
	return nil
}

func (c *ReportClientCLI) PrintReportResults(includeStdErr bool) {
	switch c.report.Metadata.State {
	case report_master.ReportState_WAITING_TO_START:
		fmt.Printf("After %d seconds the report is still waiting to start.\n", *deadlineSeconds)
		break

	case report_master.ReportState_IN_PROGRESS:
		fmt.Printf("After %d seconds the report is still in progress.\n", *deadlineSeconds)
		break

	case report_master.ReportState_COMPLETED_SUCCESSFULLY:
		fmt.Println()
		fmt.Println("Results")
		fmt.Println("=======")
		c.PrintCSVReport(includeStdErr)
		fmt.Println()
		break

	case report_master.ReportState_TERMINATED:
		fmt.Println()
		fmt.Println("Report Errors")
		fmt.Println("=======")
		for _, message := range c.reportClient.ReportErrorsToStrings(c.report, true) {
			fmt.Println(message)
		}
		fmt.Println()
	}
}

func (c *ReportClientCLI) startReport(complete bool,
	firstDayOffset int, lastDayOffset int, reportConfigId uint32) (string, error) {
	if complete {
		fmt.Printf("Generating a new report for Report Configuration %d cover all days...\n", reportConfigId)
		return c.reportClient.StartCompleteReport(reportConfigId)
	} else {
		fmt.Printf("Generating a new report for Report Configuration %d covering the relative day interval [%d, %d]...\n",
			reportConfigId, firstDayOffset, lastDayOffset)
		return c.reportClient.StartReportRelativeUtc(reportConfigId, firstDayOffset, lastDayOffset)
	}
}

func (c *ReportClientCLI) RunReportAndPrint(complete bool,
	firstDayOffset int, lastDayOffset int, reportConfigId uint32, printErrorColumn bool) {
	// Start the report
	reportId, err := c.startReport(complete, firstDayOffset, lastDayOffset, reportConfigId)
	if err != nil {
		fmt.Printf("Error while generating report: [%v]\n", err)
		return
	}

	// Fetch the report repeatedly until it is done.
	report, err := c.reportClient.GetReport(reportId, time.Duration(*deadlineSeconds)*time.Second)

	if err != nil {
		fmt.Printf("Error while fetching report: [%v]\n", err)
		return
	}
	c.report = report

	// Print it
	c.PrintReportResults(printErrorColumn)
}

func (c *ReportClientCLI) PrintHelp() {
	fmt.Println()
	fmt.Println("Cobalt command-line report client")
	fmt.Printf("Report Master URI: %s\n", *reportMasterURI)
	fmt.Printf("Project ID: %d\n", *projectID)
	fmt.Println("---------------------------------")
	fmt.Printf("help                  \t Print this help message.\n")
	fmt.Println()
	fmt.Printf("run range <firstDay> <lastDay> <cID> [errs]\n")
	fmt.Printf("                      \t Run a new report based on the ReportConfigId <cID> covering the specified interval of days.\n")
	fmt.Printf("                      \t Wait for the report to complete and then print the results to the console in CSV format.\n")
	fmt.Printf("                      \t The values <firstDay> and <lastDay> are (usually negative) integers specifying the day relative to\n")
	fmt.Printf("                      \t the current day in the UTC timezone. Thus for example to generate a report that covers the two day period\n")
	fmt.Printf("                      \t consisting of two days ago and yesterday, use <firstDay> = -2 and <lastDay> = -1.\n")
	fmt.Printf("                      \t If the token 'errs' is appended to the command the report will include a standard error column\n")
	fmt.Println()
	fmt.Printf("run full <cID> [errs] \t Run a new report based on the ReportConfigId <cID>.\n")
	fmt.Printf("                      \t Wait for the report to complete and then print the results to the console in CSV format.\n")
	fmt.Printf("                      \t The report will cover all Observations ever collected that are associated to the report.\n")
	fmt.Printf("                      \t If the token 'errs' is appended to the command the report will include a standard error column\n")
	fmt.Println()
	fmt.Printf("quit                  \t Quit.\n")
	fmt.Println()
}

// processRunRangeCommand is invoked after we already know the following:
// 3 <= len(commandTokens) <= 6
// commandTokens[0] = "run"
// commandTokens[1] = "range"
func (c *ReportClientCLI) processRunRangeCommand(commandTokens []string) {
	// Command should be of the form: run range <firstDayOffset> <lastDayOffset> <reportConfigId> [errs]
	if len(commandTokens) < 5 {
		fmt.Println("Malformed run range command. Expected at least three arguments after 'range'.")
		return
	}
	firstDayOffset, err := strconv.Atoi(commandTokens[2])
	if err != nil {
		fmt.Printf("Expected an integer instead of %s.\n", commandTokens[2])
		return
	}
	lastDayOffset, err := strconv.Atoi(commandTokens[3])
	if err != nil {
		fmt.Printf("Expected an integer instead of %s.\n", commandTokens[3])
		return
	}
	reportConfigId, err := strconv.Atoi(commandTokens[4])
	if err != nil || reportConfigId <= 0 {
		fmt.Printf("Expected a positive integer instead of %s.\n", commandTokens[4])
		return
	}

	printErrorColumn := false
	if len(commandTokens) == 6 {
		if commandTokens[5] == "errs" {
			printErrorColumn = true
		} else {
			fmt.Printf("Expected 'errs' instead of %s.\n", commandTokens[5])
			return
		}
	}

	c.RunReportAndPrint(false, firstDayOffset, lastDayOffset, uint32(reportConfigId), printErrorColumn)
}

// processRunFullCommand is invoked after we already know the following:
// 3 <= len(commandTokens) <= 6
// commandTokens[0] = "run"
// commandTokens[1] = "full"
func (c *ReportClientCLI) processRunFullCommand(commandTokens []string) {
	// Command should be of the form: run full <reportConfigId> [errs]
	if len(commandTokens) > 4 {
		fmt.Println("Malformed run full command. Expected only 2 or three arguments after 'run full'.")
		return
	}
	reportConfigId, err := strconv.Atoi(commandTokens[2])
	if err != nil || reportConfigId <= 0 {
		fmt.Printf("Expected a positive integer instead of %s.\n", commandTokens[2])
		return
	}

	printErrorColumn := false
	if len(commandTokens) == 4 {
		if commandTokens[3] == "errs" {
			printErrorColumn = true
		} else {
			fmt.Printf("Expected 'errs' instead of %s.\n", commandTokens[3])
			return
		}
	}

	c.RunReportAndPrint(true, 0, 0, uint32(reportConfigId), printErrorColumn)
}

func (c *ReportClientCLI) RunReport(commandTokens []string) {
	if len(commandTokens) < 3 || len(commandTokens) > 6 {
		fmt.Println("Malformed run command. Expected between 2 and 5 arguments.")
		return
	}

	if commandTokens[1] == "range" {
		c.processRunRangeCommand(commandTokens)
		return
	} else if commandTokens[1] == "full" {
		c.processRunFullCommand(commandTokens)
		return
	}

	fmt.Printf("Unrecognized run command: %s.\n", commandTokens[1])
	return
}

func (c *ReportClientCLI) ProcessCommand(commandTokens []string) bool {
	if len(commandTokens) == 0 {
		return true
	}

	if commandTokens[0] == "help" {
		c.PrintHelp()
		return true
	}

	if commandTokens[0] == "run" {
		c.RunReport(commandTokens)
		return true
	}

	if commandTokens[0] == "quit" {
		return false
	}

	fmt.Printf("Unrecognized command: %s\n", commandTokens[0])

	return true
}

func (c *ReportClientCLI) CommandLoop() {
	scanner := bufio.NewScanner(os.Stdin)
	for {
		fmt.Print("Command or 'help': ")
		scanner.Scan()
		line := scanner.Text()
		lineScanner := bufio.NewScanner(strings.NewReader(line))
		lineScanner.Split(bufio.ScanWords)
		tokens := []string{}
		for lineScanner.Scan() {
			token := lineScanner.Text()
			tokens = append(tokens, token)
		}
		if !c.ProcessCommand(tokens) {
			break
		}
	}
}

func (c *ReportClientCLI) ExecuteCommand() {
	var command []string
	if *firstDay != math.MaxInt64 && *lastDay != math.MaxInt64 {
		command = []string{"run", "range", fmt.Sprintf("%d", *firstDay), fmt.Sprintf("%d", *lastDay), fmt.Sprintf("%d", *reportConfigID)}
	} else {
		command = []string{"run", "full", fmt.Sprintf("%d", *reportConfigID)}
	}
	if *includeStdErrColumn {
		command = append(command, "errs")
	}
	c.ProcessCommand(command)
}

func main() {
	flag.Parse()

	if *reportMasterURI == "" {
		fmt.Println("The flag -report_master_uri is mandatory.")
		os.Exit(1)
	}

	cli := ReportClientCLI{
		reportClient: report_client.NewReportClient(uint32(*customerID), uint32(*projectID),
			*reportMasterURI, *tls, *caFile),
	}

	if *interactive {
		cli.CommandLoop()
	} else {
		cli.ExecuteCommand()
	}

}
