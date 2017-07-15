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

// Unit tests for testing both MemStore and PersistentStore interfaces

package report_client

import (
	"bytes"
	"math"
	"reflect"
	"testing"
	"time"

	"analyzer/report_master"
	"cobalt"
)

const customerId = 1
const projectId = 2
const reportConfigId = 3
const firstDayIndex = 4
const lastDayIndex = 5

var stringValuePart1 = cobalt.ValuePart{
	Data: &cobalt.ValuePart_StringValue{
		StringValue: "String Value 11",
	},
}

var stringValuePart2 = cobalt.ValuePart{
	Data: &cobalt.ValuePart_StringValue{
		StringValue: "String Value 2",
	},
}

var intValuePart1 = cobalt.ValuePart{
	Data: &cobalt.ValuePart_IntValue{
		IntValue: 42,
	},
}

var intValuePart2 = cobalt.ValuePart{
	Data: &cobalt.ValuePart_IntValue{
		IntValue: 43,
	},
}

var successfulReport = report_master.Report{
	Metadata: &report_master.ReportMetadata{
		State: report_master.ReportState_COMPLETED_SUCCESSFULLY,
	},
	Rows: &report_master.ReportRows{
		Rows: []*report_master.ReportRow{
			&report_master.ReportRow{
				RowType: &report_master.ReportRow_Histogram{
					Histogram: &report_master.HistogramReportRow{
						Value:         &intValuePart1,
						CountEstimate: 101.1,
						StdError:      3.14,
					},
				},
			},
			&report_master.ReportRow{
				RowType: &report_master.ReportRow_Histogram{
					Histogram: &report_master.HistogramReportRow{
						Value:         &stringValuePart2,
						CountEstimate: 102.2,
						StdError:      3.14,
					},
				},
			},
			&report_master.ReportRow{
				RowType: &report_master.ReportRow_Histogram{
					Histogram: &report_master.HistogramReportRow{
						Value:         &stringValuePart1,
						CountEstimate: 103.3,
						StdError:      3.14,
					},
				},
			},
			&report_master.ReportRow{
				RowType: &report_master.ReportRow_Histogram{
					Histogram: &report_master.HistogramReportRow{
						Value:         &intValuePart2,
						CountEstimate: 104.4,
						StdError:      3.14,
					},
				},
			},
		},
	},
}

// To understand why this is the expected CSV report string, see the comments
// on the Compare() function.
const expectedCSVReportString = `String Value 11,103.300,3.140
String Value 2,102.200,3.140
42,101.100,3.140
43,104.400,3.140
`

var failedReportPrimary = report_master.Report{
	Metadata: &report_master.ReportMetadata{
		State: report_master.ReportState_TERMINATED,
		InfoMessages: []*report_master.InfoMessage{
			&report_master.InfoMessage{Message: "Error message primary line 1"},
			&report_master.InfoMessage{Message: "Error message primary line 2"},
		},
		AssociatedReportIds: []string{"associated-id"},
	},
}

var failedReportAssociated = report_master.Report{
	Metadata: &report_master.ReportMetadata{
		State: report_master.ReportState_TERMINATED,
		InfoMessages: []*report_master.InfoMessage{
			&report_master.InfoMessage{Message: "Error message associated line 1"},
			&report_master.InfoMessage{Message: "Error message associated line 2"},
		},
	},
}

var expectedErrorStrings = []string{"Error message associated line 1",
	"Error message associated line 2", "Error message primary line 1",
	"Error message primary line 2"}

// fakeReportMasterStub implements ReportMasterStub by storing the arguments
// so that a test may inspect them and returning pre-specified responses.
type fakeReportMasterStub struct {
	startReportRequest  report_master.StartReportRequest
	startReportResponse report_master.StartReportResponse

	getReportRequest report_master.GetReportRequest
	report           *report_master.Report
}

func (f *fakeReportMasterStub) StartReport(request *report_master.StartReportRequest) (*report_master.StartReportResponse, error) {
	f.startReportRequest = *request
	return &f.startReportResponse, nil
}

func (f *fakeReportMasterStub) GetReport(request *report_master.GetReportRequest) (*report_master.Report, error) {
	f.getReportRequest = *request
	return f.report, nil
}

// Constructs a ReportClient that uses a fakeReportMasterStub as its
// ReportMasterStub. Returns the ReportClient and the stub.
func makeFakeClient() (reportClient ReportClient, fakeStub *fakeReportMasterStub) {
	fakeStub = new(fakeReportMasterStub)
	reportClient = ReportClient{
		CustomerId: customerId,
		ProjectId:  projectId,
		stub:       fakeStub,
	}
	return
}

// Tests the function StartCompleteReport.
func TestStartCompleteReport(t *testing.T) {
	reportClient, fakeStub := makeFakeClient()
	fakeStub.startReportResponse.ReportId = "my-report-id"
	reportId, err := reportClient.StartCompleteReport(reportConfigId)
	if err != nil {
		t.Errorf("Error returned from StartReport: %v", err)
	}
	if fakeStub.startReportRequest.CustomerId != customerId {
		t.Errorf("CustomerId=%s", fakeStub.startReportRequest.CustomerId)
	}
	if fakeStub.startReportRequest.ProjectId != projectId {
		t.Errorf("ProjectId=%s", fakeStub.startReportRequest.ProjectId)
	}
	if fakeStub.startReportRequest.ReportConfigId != reportConfigId {
		t.Errorf("ReportconfigId=%s", fakeStub.startReportRequest.ReportConfigId)
	}
	if fakeStub.startReportRequest.FirstDayIndex != 0 {
		t.Errorf("FirstDayIndex=%s", fakeStub.startReportRequest.FirstDayIndex)
	}
	if fakeStub.startReportRequest.LastDayIndex != math.MaxUint32 {
		t.Errorf("LastDayIndex=%s", fakeStub.startReportRequest.LastDayIndex)
	}
	if reportId != "my-report-id" {
		t.Errorf("reportId=%s", reportId)
	}
}

// Tests the function StartReport.
func TestStartReport(t *testing.T) {
	reportClient, fakeStub := makeFakeClient()
	fakeStub.startReportResponse.ReportId = "my-report-id"
	reportId, err := reportClient.StartReport(reportConfigId, firstDayIndex, lastDayIndex)
	if err != nil {
		t.Errorf("Error returned from StartReport: %v", err)
	}
	if fakeStub.startReportRequest.CustomerId != customerId {
		t.Errorf("CustomerId=%s", fakeStub.startReportRequest.CustomerId)
	}
	if fakeStub.startReportRequest.ProjectId != projectId {
		t.Errorf("ProjectId=%s", fakeStub.startReportRequest.ProjectId)
	}
	if fakeStub.startReportRequest.ReportConfigId != reportConfigId {
		t.Errorf("ReportconfigId=%s", fakeStub.startReportRequest.ReportConfigId)
	}
	if fakeStub.startReportRequest.FirstDayIndex != firstDayIndex {
		t.Errorf("FirstDayIndex=%s", fakeStub.startReportRequest.FirstDayIndex)
	}
	if fakeStub.startReportRequest.LastDayIndex != lastDayIndex {
		t.Errorf("LastDayIndex=%s", fakeStub.startReportRequest.LastDayIndex)
	}
	if reportId != "my-report-id" {
		t.Errorf("reportId=%s", reportId)
	}
}

// Tests the function GetReport.
func TestGetReport(t *testing.T) {
	reportClient, fakeStub := makeFakeClient()
	fakeStub.report = &successfulReport
	report, err := reportClient.GetReport("my-report-id", 0)
	if err != nil {
		t.Errorf("Error returned from GetReport: %v", err)
	}
	if fakeStub.getReportRequest.ReportId != "my-report-id" {
		t.Errorf("ReportId=%s", fakeStub.getReportRequest.ReportId)
	}
	if report != &successfulReport {
		t.Errorf("report != successfulReport")
	}
}

// Tests the function WriteCSVReport
func TestWriteCSVReport(t *testing.T) {
	var buffer bytes.Buffer
	includeStdErr := true
	err := WriteCSVReport(&buffer, &successfulReport, includeStdErr)
	if err != nil {
		t.Errorf("Error returned from WriteCSVReport: %v", err)
	}
	if buffer.String() != expectedCSVReportString {
		t.Errorf("Got CSV [%s]", buffer.String())
	}
}

func TestReportErrorToStrings(t *testing.T) {
	reportClient, fakeStub := makeFakeClient()
	fakeStub.report = &failedReportAssociated
	errorStrings := reportClient.ReportErrorsToStrings(&failedReportPrimary, true)

	if fakeStub.getReportRequest.ReportId != "associated-id" {
		t.Errorf("ReportId=%s", fakeStub.getReportRequest.ReportId)
	}
	if !reflect.DeepEqual(expectedErrorStrings, errorStrings) {
		t.Errorf("errorStrings=%v", errorStrings)
	}
}

func TestDayIndex(t *testing.T) {
	// This unix timestamp corresponds to Friday Dec 2, 2016 in UTC
	// and Thursday Dec 1, 2016 in Pacific time.
	const someTimestamp = 1480647356
	someTime := time.Unix(someTimestamp, 0)

	// This is the day index for Friday Dec 2, 2016
	const utcDayIndex = 17137

	// This is the day index for Thurs Dec 1, 2016
	const pacificDayIndex = 17136

	if dayIndexUtc(someTime) != utcDayIndex {
		t.Errorf("oops")
	}
	name, _ := time.Now().Zone()
	if name == "PDT" {
		// Only execute this check when the test is being run in the Pacific timezone.
		dayIndexLocal := dayIndexLocal(someTime)
		if dayIndexLocal != pacificDayIndex {
			t.Errorf("dayIndexLocal(someTime=%d", dayIndexLocal)
		}
	}
}
