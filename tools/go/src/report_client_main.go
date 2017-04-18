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

func (c *ReportClientCLI) RunReportAndPrint(reportConfigId uint32, printErrorColumn bool) {
	// Start the report
	fmt.Printf("Generating a new report for Report Configuration %d...\n", reportConfigId)
	reportId, err := c.reportClient.StartCompleteReport(reportConfigId)
	if err != nil {
		fmt.Printf("StartCompleteReport() returned an error: [%v]\n", err)
		return
	}

	// Fetch the report repeatedly until it is done.
	report, err := c.reportClient.GetReport(reportId, time.Duration(*deadlineSeconds)*time.Second)

	if err != nil {
		fmt.Printf("GetReport() returned an error: [%v]\n", err)
		return
	}
	c.report = report

	// Print it
	c.PrintReportResults(printErrorColumn)
}

func (c *ReportClientCLI) PrintHelp() {
	fmt.Println()
	fmt.Println("Cobalt command-line report client")
	fmt.Println("---------------------------------")
	fmt.Printf("help                  \t Print this help message.\n")
	fmt.Println()
	fmt.Printf("run full <cID> [errs] \t Run a new report based on the ReportConfigId <cID> which is a postivie integer.\n")
	fmt.Printf("                      \t Wait for the report to complete and then print the results to the console in CSV format.\n")
	fmt.Printf("                      \t The report will cover all Observations ever collected for the associated metrics.\n")
	fmt.Printf("                      \t If the token 'errs' is appended to the command the report will include a standard error column\n")
	fmt.Println()
	fmt.Printf("quit                  \t Quit.\n")
	fmt.Println()
}

func (c *ReportClientCLI) RunReport(commandTokens []string) {
	if len(commandTokens) < 3 || len(commandTokens) > 4 {
		fmt.Println("Malformed run command. Expected 2 or 3 arguments.")
		return
	}
	if commandTokens[1] != "full" {
		fmt.Printf("Unrecognized run command: %s.\n", commandTokens[1])
		return
	}

	reportConfigID, err := strconv.Atoi(commandTokens[2])
	if err != nil || reportConfigID <= 0 {
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

	c.RunReportAndPrint(uint32(reportConfigID), printErrorColumn)

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
	command := []string{"run", "full", fmt.Sprintf("%d", *reportConfigID)}
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
