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
An end-to-end test of Cobalt. This test assumes the existence of a running
Cobalt system. The URIs of the Shuffler, the Analyzer Service and the
ReportMaster are passed in to the test as flags. Usually this test is
invoked via the command "cobaltb.py test --tests=e2e" which invokes
"tools/test_runner.py" which uses "tools/process_starter.py" to start the
Cobalt processes prior to invoking this test.

The test uses the |cobalt_test_app| program in order to encode values into
observations and send those observations to the Shuffler. It then uses
the |query_observations| tool to query the Observation Store and it waits
until all of the observations have arrived at the Observation Store. It
then uses the report_client to contact the ReportMaster and request
that a report be generated and wait for the report to be generated. It then
validates the report.

The test assumes particular contents of the Cobalt registration system.
By default "tools/process_starter.py" will cause the Cobalt processes to
use the configuration files located in <source root>/config/demo. Thus
this test must be kept in sync with the contents of those files. Here
we include a copy of the relevant parts of  those files for reference:

#### Metric (1, 1, 1)
element {
  customer_id: 1
  project_id: 1
  id: 1
  name: "Fuchsia Popular URLs"
  description: "This is a fictional metric used for the development of Cobalt."
  time_zone_policy: LOCAL
  parts {
    key: "url"
    value {
      description: "A URL."
      data_type: STRING
    }
  }
}

#### Metric (1, 1, 2)
element {
  customer_id: 1
  project_id: 1
  id: 2
  name: "Fuchsia Usage by Hour"
  description: "This is a fictional metric used for the development of Cobalt."
  time_zone_policy: LOCAL
  parts {
    key: "hour"
    value {
      description: "An integer from 0 to 23 representing the hour of the day."
      data_type: INT
    }
  }
}

#### Encoding (1, 1, 1)
element {
  customer_id: 1
  project_id: 1
  id: 1
  forculus {
    threshold: 20
    epoch_type: DAY
  }
}

#### Encoding (1, 1, 2)
element {
  customer_id: 1
  project_id: 1
  id: 2
  basic_rappor {
    prob_0_becomes_1: 0.1
    prob_1_stays_1: 0.9
    int_range_categories: {
      first: 0
      last:  23
    }
  }
}

#### ReportConfig (1, 1, 1)
element {
  customer_id: 1
  project_id: 1
  id: 1
  name: "Fuchsia Popular URLs"
  description: "This is a fictional report used for the development of Cobalt."
  metric_id: 1
  variable {
    metric_part: "url"
  }
  aggregation_epoch_type: DAY
  report_delay_days: 1
}

#### ReportConfig (1, 1, 2)
element {
  customer_id: 1
  project_id: 1
  id: 2
  name: "Fuchsia Usage by Hour"
  description: "This is a fictional report used for the development of Cobalt."
  metric_id: 2
  variable {
    metric_part: "hour"
  }
  aggregation_epoch_type: WEEK
  report_delay_days: 5
}

*/

package main

import (
	"bytes"
	"flag"
	"fmt"
	"os/exec"
	"strconv"
	"testing"
	"time"

	"analyzer/report_master"
	"github.com/golang/glog"
	"report_client"
)

const (
	customerId = 1
	projectId  = 1

	urlMetricId  = 1
	hourMetricId = 2

	forculusEncodingConfigId    = 1
	basicRapporEncodingConfigId = 2

	forculusUrlReportConfigId     = 1
	basicRapporHourReportConfigId = 2

	hourMetricPartName = "hour"
	urlMetricPartName  = "url"
)

var (
	observationQuerierPath = flag.String("observation_querier_path", "", "The full path to the Observation querier binary")
	testAppPath            = flag.String("test_app_path", "", "The full path to the Cobalt test app binary")

	analyzerUri     = flag.String("analyzer_uri", "", "The URI of the Analyzer Service")
	reportMasterUri = flag.String("report_master_uri", "", "The URI of the Report Master")
	shufflerUri     = flag.String("shuffler_uri", "", "The URI of the Shuffler")

	analyzerPkPemFile = flag.String("analyzer_pk_pem_file", "", "Path to a file containing a PEM encoding of the public key of the Analyzer")
	shufflerPkPemFile = flag.String("shuffler_pk_pem_file", "", "Path to a file containing a PEM encoding of the public key of the Shuffler")

	subProcessVerbosity = flag.Int("sub_process_v", 0, "-v verbosity level to pass to sub-processes")

	reportClient *report_client.ReportClient
)

func init() {
	flag.Parse()

	reportClient = report_client.NewReportClient(customerId, projectId, *reportMasterUri, false, "")
}

// A ValuePart represents part of an input to the Cobalt encoder. It specifies
// that the given integer or string should be encoded using the given
// EncodingConfig and associated with the given metric part name.
type ValuePart struct {
	// The name of the metric part this value is for.
	partName string

	// The string representation of the value. If the value is of integer
	// type this should be the representation using strconv.Itoa.
	repr string

	// The EncodingConfig id.
	encoding uint32
}

// String returns a string representation of the ValuePart in the form
// <partName>:<repr>:<encoding>. This is the form accepted as a flag to
// the Cobalt test application.
func (p *ValuePart) String() string {
	return p.partName + ":" + p.repr + ":" + strconv.Itoa(int(p.encoding))
}

// FlagString builds a string appropriate to use as a flag value to the Cobalt
// test application.
func flagString(values []ValuePart) string {
	var buffer bytes.Buffer
	for i := 0; i < len(values); i++ {
		if i > 0 {
			buffer.WriteString(",")
		}
		buffer.WriteString(values[i].String())
	}
	return buffer.String()
}

// Invokes the "query_observations" command in order to query the ObservationStore
// to determine the number of Observations contained in the store for the
// given metric. |maxNum| bounds the query so that the returned value will
// always be less than or equal to maxNum.
func getNumObservations(metricId uint32, maxNum uint32) (uint32, error) {
	cmd := exec.Command(*observationQuerierPath,
		"-nointeractive",
		"-for_testing_only_use_bigtable_emulator",
		"-logtostderr", fmt.Sprintf("-v=%d", *subProcessVerbosity),
		"-metric", strconv.Itoa(int(metricId)),
		"-max_num", strconv.Itoa(int(maxNum)))
	out, err := cmd.Output()
	if err != nil {
		stdErrMessage := ""
		if exitError, ok := err.(*exec.ExitError); ok {
			stdErrMessage = string(exitError.Stderr)
		}
		return 0, fmt.Errorf("Error returned from query_observations process: [%v] %s", err, stdErrMessage)
	}
	num, err := strconv.Atoi(string(out))
	if err != nil {
		return 0, fmt.Errorf("Unable to parse output of query_observations as an integer: error=[%v] output=[%v]", err, out)
	}
	if num < 0 {
		return 0, fmt.Errorf("Expected non-negative integer received %d", num)
	}
	return uint32(num), nil
}

// Queries the Observation Store for the number of observations for the given
// metric. If there is an error or the number of observations found is greater
// then the |expectedNum| returns a non-nil error. If the number of observations
// found is equal to the |expectedNum| returns nil (indicating success.) Otherwise
// the number of observations found is less than the |expectedNum|. In this case
// this function sleeps for one second and then tries again, repeating that for
// up to 30 attempts and finally returns a non-nil error.
func waitForObservations(metricId uint32, expectedNum uint32) error {
	for trial := 0; trial < 30; trial++ {
		num, err := getNumObservations(metricId, expectedNum+1)
		if err != nil {
			return err
		}
		if num == expectedNum {
			return nil
		}
		if num > expectedNum {
			return fmt.Errorf("Expected %d got %d", expectedNum, num)
		}
		glog.V(3).Infof("Observation store has %d observations. Waiting for %d...", num, expectedNum)
		time.Sleep(time.Second)
	}
	return fmt.Errorf("After 30 attempts the number of observations was still not the expected number of %d", expectedNum)
}

// sendObservations uses the cobalt_test_app to encode the given values into observations and send the
// observations to the Shuffler or the Analyzer.
func sendObservations(metricId uint32, values []ValuePart, skipShuffler bool, numClients uint) error {
	cmd := exec.Command(*testAppPath,
		"-mode", "send-once",
		"-analyzer_uri", *analyzerUri,
		"-analyzer_pk_pem_file", *analyzerPkPemFile,
		"-shuffler_uri", *shufflerUri,
		"-shuffler_pk_pem_file", *shufflerPkPemFile,
		"-logtostderr", fmt.Sprintf("-v=%d", *subProcessVerbosity),
		"-metric", strconv.Itoa(int(metricId)),
		"-num_clients", strconv.Itoa(int(numClients)),
		fmt.Sprintf("-skip_shuffler=%t", skipShuffler),
		"-values", flagString(values))
	stdoutStderr, err := cmd.CombinedOutput()
	message := string(stdoutStderr)
	if len(message) > 0 {
		fmt.Printf("%s", stdoutStderr)
	}
	return err
}

// sendForculusUrlObservations sends Observations containing a Forculus encryption of the
// given |url| to the Shuffler. |numClients| different, independent
// observations will be sent.
func sendForculusUrlObservations(url string, numClients uint, t *testing.T) {
	const skipShuffler = false
	values := []ValuePart{
		ValuePart{
			urlMetricPartName,
			url,
			forculusEncodingConfigId,
		},
	}
	if err := sendObservations(urlMetricId, values, skipShuffler, numClients); err != nil {
		t.Fatalf("url=%s, numClient=%d, err=%v", url, numClients, err)
	}
}

// sendBasicRapporHourObservations sends Observations containing a Basic RAPPOR encoding of the
// given |hour| to the Shuffler. |numClients| different, independent observations
// will be sent.
func sendBasicRapporHourObservations(hour int, numClients uint, t *testing.T) {
	const skipShuffler = false
	values := []ValuePart{
		ValuePart{
			hourMetricPartName,
			strconv.Itoa(hour),
			basicRapporEncodingConfigId,
		},
	}
	if err := sendObservations(hourMetricId, values, skipShuffler, numClients); err != nil {
		t.Fatalf("hour=%d, numClient=%d, err=%v", hour, numClients, err)
	}
}

// getReport asks the ReportMaster to start a new report for the given |reportConfigId|
// that spans all day indices. It then waits for the report generation to complete
// and returns the Report.
func getReport(reportConfigId uint32, includeStdErr bool, t *testing.T) *report_master.Report {
	reportId, err := reportClient.StartCompleteReport(reportConfigId)
	if err != nil {
		t.Fatalf("reportConfigId=%d, err=%v", reportConfigId, err)
	}

	report, err := reportClient.GetReport(reportId, 10*time.Second)
	if err != nil {
		t.Fatalf("reportConfigId=%d, err=%v", reportConfigId, err)
	}

	return report
}

// getCSVReport asks the ReportMaster to start a new report for the given |reportConfigId|
// that spans all day indices. It then waits for the report generation to complete
// and returns the report in CSV format.
func getCSVReport(reportConfigId uint32, includeStdErr bool, t *testing.T) string {
	report := getReport(reportConfigId, includeStdErr, t)

	csv, err := report_client.WriteCSVReportToString(report, includeStdErr)
	if err != nil {
		t.Fatalf("reportConfigId=%d, err=%v", reportConfigId, err)
	}
	return csv
}

// We run the full Cobalt pipeline using Metric 1, Encoding Config 1 and
// Report Config 1. This uses Forculus with a threshold of 20 to count
// URLs.
func TestForculusEncodingOfUrls(t *testing.T) {
	// We send some observations to the Shuffler.
	sendForculusUrlObservations("www.AAAA.com", 18, t)
	sendForculusUrlObservations("www.BBBB.com", 19, t)
	sendForculusUrlObservations("www.CCCC.com", 20, t)
	sendForculusUrlObservations("www.DDDD.com", 21, t)

	// We have not yet sent 100 observations and the Shuffler's threshold is
	// set to 100 so we except no observations to have been sent to the
	// Analyzer yet.
	numObservations, err := getNumObservations(1, 10)
	if err != nil {
		t.Fatalf("Error returned from getNumObservations[%v]", err)
	}
	if numObservations != 0 {
		t.Fatalf("Expected no observations in the Observation store yet but got %d", numObservations)
	}

	// We send additional observations to the Shuffler. This crosses the Shuffler's
	// threshold and so all observations should now be sent to the Analyzer.
	sendForculusUrlObservations("www.EEEE.com", 22, t)
	sendForculusUrlObservations("www.FFFF.com", 23, t)

	// There should now be 123 Observations sent to the Analyzer for metric 1.
	// We wait for them.
	if err := waitForObservations(1, 123); err != nil {
		t.Fatalf("%s", err)
	}

	// Finally we will run a report. This is the expected output of the report.
	const expectedCSV = `www.CCCC.com,20.000
www.DDDD.com,21.000
www.EEEE.com,22.000
www.FFFF.com,23.000
`

	// Generate the report, fetch it as a CSV, check it.
	csv := getCSVReport(forculusUrlReportConfigId, false, t)
	if csv != expectedCSV {
		t.Errorf("Got csv:[%s]", csv)
	}
}

// We run the full Cobalt pipeline using Metric 2, Encoding Config 2 and
// Report Config 2. This uses Basic RAPPOR with integer categories for the
// 24 hours of the day.
func TestBasicRapporEncodingOfHours(t *testing.T) {
	sendBasicRapporHourObservations(8, 501, t)
	sendBasicRapporHourObservations(9, 1002, t)
	sendBasicRapporHourObservations(10, 503, t)
	sendBasicRapporHourObservations(16, 504, t)
	sendBasicRapporHourObservations(17, 1005, t)
	sendBasicRapporHourObservations(18, 506, t)

	// There should now be 4021 Observations sent to the Analyzer for metric 2.
	// We wait for them.
	if err := waitForObservations(2, 4021); err != nil {
		t.Fatalf("%s", err)
	}

	report := getReport(basicRapporHourReportConfigId, true, t)
	if report.Metadata.State != report_master.ReportState_COMPLETED_SUCCESSFULLY {
		t.Fatalf("report.Metadata.State=%v", report.Metadata.State)
	}
	rows := report_client.ReportToStrings(report, true)
	if rows == nil {
		t.Fatalf("rows is nil")
	}
	if len(rows) != 24 {
		t.Fatalf("len(rows)=%d", len(rows))
	}

	for hour := 0; hour <= 23; hour++ {
		if len(rows[hour]) != 3 {
			t.Fatalf("len(rows[hour])=%d", len(rows[hour]))
		}
		if rows[hour][0] != strconv.Itoa(hour) {
			t.Errorf("Expected %s, got %s", strconv.Itoa(hour), rows[hour][0])
		}
		val, err := strconv.ParseFloat(rows[hour][1], 32)
		if err != nil {
			t.Errorf("Error parsing %s as float: %v", rows[hour][1], err)
			continue
		}
		switch hour {
		case 8:
			fallthrough
		case 10:
			fallthrough
		case 16:
			fallthrough
		case 18:
			if val < 10.0 || val > 1000.0 {
				t.Errorf("For hour %d unexpected val: %v", hour, val)
			}
		case 9:
			fallthrough
		case 17:
			if val < 500.0 || val > 2000.0 {
				t.Errorf("For hour %d unexpected val: %v", hour, val)
			}
		default:
			if val > 100.0 {
				t.Errorf("Val larger than expected: %v", val)
				continue
			}
		}
		if rows[hour][2] != "23.779" {
			t.Errorf("rows[hour][2]=%s", rows[hour][2])
		}
	}
}
