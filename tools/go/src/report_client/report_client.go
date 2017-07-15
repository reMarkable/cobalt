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
Package report_client implements a user-friendly wrapper around the
auto-generated gRPC client for the ReportMaster API.
*/
package report_client

import (
	"bytes"
	"encoding/csv"
	"fmt"
	"io"
	"math"
	"sort"
	"strings"
	"time"

	"analyzer/report_master"
	"cobalt"
	"github.com/golang/glog"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
)

// The ReportMasterStub interface provides an abstraction layer that allows
// us to mock out the gRPC stub in tests.
type ReportMasterStub interface {
	StartReport(*report_master.StartReportRequest) (*report_master.StartReportResponse, error)
	GetReport(*report_master.GetReportRequest) (*report_master.Report, error)
}

// gRPCReportMasterStub implements the interface ReportMasterStub by actually
// using a real gRPC stub.
type gRPCReportMasterStub struct {
	grpcStub report_master.ReportMasterClient
}

func (s *gRPCReportMasterStub) StartReport(request *report_master.StartReportRequest) (*report_master.StartReportResponse, error) {
	return s.grpcStub.StartReport(context.Background(), request)
}

func (s *gRPCReportMasterStub) GetReport(request *report_master.GetReportRequest) (*report_master.Report, error) {
	return s.grpcStub.GetReport(context.Background(), request)
}

// An instance of ReportClient is used to communicate with the ReportMaster.
// It encapsulates a fixed customer ID and project ID.
type ReportClient struct {
	CustomerId uint32
	ProjectId  uint32

	stub ReportMasterStub
}

// NewReportClient constructs  a ReportClient connected to the ReportMaster Service at the given |uri|.
// A fixed |customerId| and |projectId| is specified.
//
// If |tls| is false an insecure connection is used, and the remaining
// parameters or ignored, otherwise TLS is used
//
// |caFile| is optional. If non-empty it should specify the path to a file
// containing a PEM encoding of root certificates to use for TLS.
//
// Logs and crashes on any failure.
func NewReportClient(customerId uint32, projectId uint32, uri string, tls bool, caFile string) *ReportClient {
	grpcStubImpl := gRPCReportMasterStub{}

	client := ReportClient{
		CustomerId: customerId,
		ProjectId:  projectId,
		stub:       &grpcStubImpl,
	}

	var opts []grpc.DialOption
	if tls {
		var creds credentials.TransportCredentials
		if caFile != "" {
			var err error
			creds, err = credentials.NewClientTLSFromFile(caFile, "")
			if err != nil {
				glog.Fatalf("Failed to create TLS credentials: %v", err)
			}
		} else {
			creds = credentials.NewClientTLSFromCert(nil, "")
		}
		opts = append(opts, grpc.WithTransportCredentials(creds))
	} else {
		opts = append(opts, grpc.WithInsecure())
	}
	opts = append(opts, grpc.WithBlock())
	opts = append(opts, grpc.WithTimeout(10*time.Second))

	glog.Infoln("Dialing ", uri, "...")
	conn, err := grpc.Dial(uri, opts...)
	if err != nil {
		glog.Fatalf("Connect to server failed: %v", err)
	}

	grpcStubImpl.grpcStub = report_master.NewReportMasterClient(conn)
	return &client
}

// StartCompleteReport invokes StartReport using the infinite interval
// of day indices.
func (c *ReportClient) StartCompleteReport(reportConfigId uint32) (string, error) {
	return c.StartReport(reportConfigId, 0, math.MaxUint32)
}

// StartReportRelativeLocal invokes StartReport using the interval of days specified by firstDayOffset and lastDayOffset.
// The two offsets are added to the current day index in the local timezone in order to form the firstDayIndex and
// lastDayIndex that are passed to StartReport. Thus for example to obtain a report that covers the two day period
// consisting of two-days-ago and yesterday invoke this method with firstDayOffset = -2 and lastDayOffset = -1.
// The values of firstDayOffset and lastDayOffset should ordinarily be non-positive numbers since usually one would
// like to run a report that covers time periods in the past.
func (c *ReportClient) StartReportRelativeLocal(reportConfigId uint32, firstDayOffset int, lastDayOffset int) (string, error) {
	today := CurrentDayIndexLocal()
	return c.StartReport(reportConfigId, uint32(int(today)+firstDayOffset), uint32(int(today)+lastDayOffset))
}

// StartReportRelativeUtc invokes StartReport using the interval of days specified by firstDayOffset and lastDayOffset.
// The two offsets are added to the current day index in the Utc timezone in order to form the firstDayIndex and
// lastDayIndex that are passed to StartReport. Thus for example to obtain a report that covers the two day period
// consisting of two-days-ago and yesterday invoke this method with firstDayOffset = -2 and lastDayOffset = -1.
// The values of firstDayOffset and lastDayOffset should ordinarily be non-positive numbers since usually one would
// like to run a report that covers time periods in the past.
func (c *ReportClient) StartReportRelativeUtc(reportConfigId uint32, firstDayOffset int, lastDayOffset int) (string, error) {
	today := CurrentDayIndexUtc()
	return c.StartReport(reportConfigId, uint32(int(today)+firstDayOffset), uint32(int(today)+lastDayOffset))
}

// StartReport starts a report that covers the specified interval of day indices.
// A report for the given |reportConfigId| is started. The
// returned string is the unique report ID, which may be passed to GetReport(),
// or a non-nil error.
func (c *ReportClient) StartReport(reportConfigId uint32, firstDayIndex uint32, lastDayIndex uint32) (string, error) {
	request := report_master.StartReportRequest{
		CustomerId:     c.CustomerId,
		ProjectId:      c.ProjectId,
		ReportConfigId: reportConfigId,
		FirstDayIndex:  firstDayIndex,
		LastDayIndex:   lastDayIndex,
	}

	response, err := c.stub.StartReport(&request)

	if err != nil {
		return "", err
	}
	return response.ReportId, nil
}

// GetReport queries for the report with the given |reportId|.
// The report meta-data is fetched repeatedly until the report is finished,
// or until the specified maximum |wait| time. The caller may inspect the
// |State| of the |Metadata| of the returned report to see whether or not
// the report is complete. Returns the Report or a non-nil error.
func (c *ReportClient) GetReport(reportId string, wait time.Duration) (*report_master.Report, error) {
	sleepDuration := 500 * time.Millisecond
	if wait < time.Second {
		sleepDuration = wait / 2
	}

	request := report_master.GetReportRequest{
		ReportId: reportId,
	}
	t0 := time.Now()
	var report *report_master.Report
	var err error
	for {
		report, err = c.stub.GetReport(&request)
		if err != nil {
			return nil, err
		}
		if report.Metadata.State != report_master.ReportState_IN_PROGRESS &&
			report.Metadata.State != report_master.ReportState_WAITING_TO_START {
			break
		}

		t1 := time.Now()
		if (t1.Sub(t0))+sleepDuration >= wait {
			break
		}
		glog.Info(fmt.Sprintf("Report not yet complete. Sleeping for %v.\n", sleepDuration))
		time.Sleep(sleepDuration)
	}

	return report, nil
}

// ReportErrorsToStrings returns the list of human-readable error messages associated with the given |report|
// and, optionally, its associated reports. If |includeAssociatedReportErrors| is true and the given
// report has associated reports, then the associated reports will first be fetched using the
// GetReport() method. Any error messages from the associated reports will be listed before
// the error messages for the given report.
func (c *ReportClient) ReportErrorsToStrings(report *report_master.Report, includeAssociatedReportErrors bool) []string {
	var result = []string{}
	if includeAssociatedReportErrors {

		for _, associatedId := range report.Metadata.AssociatedReportIds {
			associatedReport, err := c.GetReport(associatedId, 0)
			if err == nil {
				result = append(result, c.ReportErrorsToStrings(associatedReport, false)...)
			}
		}

	}

	for _, message := range report.Metadata.InfoMessages {
		result = append(result, message.Message)
	}
	return result
}

// valuePartToString returns a human-readable string representing the given ValuePart.
func valuePartToString(val *cobalt.ValuePart) string {
	if x, ok := val.GetData().(*cobalt.ValuePart_StringValue); ok {
		return x.StringValue
	}
	if x, ok := val.GetData().(*cobalt.ValuePart_IntValue); ok {
		return fmt.Sprintf("%v", x.IntValue)
	}
	// We won't try to display the contents of a BLOB.
	return "[blob]"
}

// ReportRowToStrings returns a list of human-readable strings that represent the given |row|.
// The format of the row depends on the type of report row it is.
func ReportRowToStrings(row *report_master.ReportRow, includeStdErr bool) []string {
	if histogramRow := row.GetHistogram(); histogramRow != nil {
		return HistogramReportRowToStrings(histogramRow, includeStdErr)
	}
	glog.Fatalf("Unknown report row type %t", row)
	return nil
}

// HistogramReportRowToStrings returns a list of human-readable strings that represent the given |row|.
// The first element of the returned list will be the row's |Value|.
// The next element of the list will be the row's |CountEstimate|.
// If |includeStdErr| is true the final element of the list will be the row's
// |StdError|.
func HistogramReportRowToStrings(row *report_master.HistogramReportRow, includeStdErr bool) []string {
	result := []string{}
	if row.GetValue() != nil {
		result = append(result, valuePartToString(row.Value))
	}

	result = append(result, fmt.Sprintf("%.3f", math.Max(0, float64(row.CountEstimate))))
	if includeStdErr {
		result = append(result, fmt.Sprintf("%.3f", row.StdError))
	}
	return result
}

// CompareValueParts compares two |ValuePart|s for the purpose of sorting.
// Returns -1, 0 or 1 as v1 is respectively less than, equivalent to,
// or greater than v2.
//
// Compares number and strings in natural order. For other comparisons
// we make the following arbitrary choices for the sake of concreteness:
// (a) A nil is less than a non-nil, two nils are equivalent
// (b) A string is less than an integer is less than a blob
// (c) Two blobs are  equivalent
func CompareValueParts(v1, v2 *cobalt.ValuePart) int {
	// If both values are missing they are equal.
	if (v1 == nil) && (v2 == nil) {
		return 0
	}

	// A nil is less than a non-nil
	if v1 == nil {
		return -1
	}

	if v2 == nil {
		return 1
	}

	// See if the values are string values
	string1, ok1 := v1.GetData().(*cobalt.ValuePart_StringValue)
	string2, ok2 := v2.GetData().(*cobalt.ValuePart_StringValue)

	// Compare two string values naturally.
	if ok1 && ok2 {
		return strings.Compare(string1.StringValue, string2.StringValue)
	}

	// A string is less than a non-string
	if ok1 {
		return -1
	}
	if ok2 {
		return 1
	}

	// See if the two values are integers
	int1, ok1 := v1.GetData().(*cobalt.ValuePart_IntValue)
	int2, ok2 := v2.GetData().(*cobalt.ValuePart_IntValue)

	// Compare two integers naturally.
	if ok1 && ok2 {
		if int1.IntValue > int2.IntValue {
			return 1
		}
		if int1.IntValue < int2.IntValue {
			return -1
		}
		return 0
	}

	// An int is less than a blob
	if ok1 {
		return -1
	}
	if ok2 {
		return 1
	}

	// Two blobs are equal.
	return 0

}

func compareHistogramRows(a, b *report_master.HistogramReportRow) int {
	if a == nil || b == nil {
		return 1
	}
	// We just compare the two values.
	return CompareValueParts(a.GetValue(), b.GetValue())
}

// ByValues implements the sort.Interface interface.
// It is used to sort the rows of a report by their values.
type ByValues []*report_master.ReportRow

func (v ByValues) Len() int      { return len(v) }
func (v ByValues) Swap(i, j int) { v[i], v[j] = v[j], v[i] }

// We compare ReportRows by their values, lexicographcially.
func (v ByValues) Less(i, j int) bool {
	var difference int
	if histogramRow := v[i].GetHistogram(); histogramRow != nil {
		difference = compareHistogramRows(histogramRow, v[j].GetHistogram())
	} else {
		glog.Fatalf("Unknown report row type %t", v[i])
	}
	return difference < 0
}

// ReportRowsSortedByValues returns a sorted slice of ReportRows.
// The rows of are sorted in increasing order of their values.
// It is possible for nil to be returned if there are not ReportRows.
func ReportRowsSortedByValues(report *report_master.Report, includeStdErr bool) []*report_master.ReportRow {
	rows := report.GetRows().GetRows()
	if rows != nil {
		sort.Sort(ByValues(rows))
	}
	return rows
}

// ReportToStrings returns a sorted list of human-readable report rows.
// Each element of the returned list represents  a row of the report.
// The rows of are sorted in increasing order of their values.
// Each row is itself a list of strings as specified by ReportRowToStrings.
func ReportToStrings(report *report_master.Report, includeStdErr bool) [][]string {
	result := [][]string{}
	rows := ReportRowsSortedByValues(report, includeStdErr)
	if rows != nil {
		for _, row := range rows {
			result = append(result, ReportRowToStrings(row, includeStdErr))
		}
	}
	return result
}

// WriteCSVReport writes a comma-separated values representation of the
// given |report| to the given |writer|. Each line represents a row of the
// report. The lines are sorted in increasing order by value. Each row
// contains 2, 3 or 4 fields. The first two fields are the rows Value,
// or its Value2, or both, depending on which of these is present.
// The next field is the row's CountEstimate. If |includeStdErr| is true
// the final field will be the row's StdErr.
func WriteCSVReport(w io.Writer, report *report_master.Report, includeStdErr bool) error {
	csvWriter := csv.NewWriter(w)
	err := csvWriter.WriteAll(ReportToStrings(report, includeStdErr))
	if err != nil {
		return err
	}
	csvWriter.Flush()
	return nil
}

// WriteCSVReportToString writes a comma-separated values representation of the
// given |report| and returns it as a string. See comments at WriteCSVReport
// for more details.
func WriteCSVReportToString(report *report_master.Report, includeStdErr bool) (csv string, err error) {
	var buffer bytes.Buffer
	if err = WriteCSVReport(&buffer, report, includeStdErr); err != nil {
		return
	}
	csv = buffer.String()
	return
}

const unixSecondsPerDay = 60 * 60 * 24

// See util/datetime_util.h for an explanation of Cobalt's notion of day index.

// dayIndexUtc returns the day index for the given time interpretted in Utc.
func dayIndexUtc(t time.Time) uint32 {
	return uint32(t.Unix() / unixSecondsPerDay)
}

// dayIndexLocal reutrns the day index for the given time interpretted in
// the local time zone.
func dayIndexLocal(t time.Time) uint32 {
	return dayIndexUtc(t.Add(time.Duration(localOffsetSeconds()) * time.Second))
}

// localOffsetSeconds returns the difference between the local time and
// the UTC time in seconds. In the Pacific timezone it returns a negative
// number.
func localOffsetSeconds() int {
	_, offset := time.Now().Zone()
	return offset
}

// CurrentDayIndexUtc returns the current day index in the UTC timezone.
func CurrentDayIndexUtc() uint32 {
	return dayIndexUtc(time.Now())
}

// CurrentDayIndexLocal returns the current day index in the local timezone.
func CurrentDayIndexLocal() uint32 {
	return dayIndexLocal(time.Now())
}
