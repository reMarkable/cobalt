// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package stackdriver

import (
	"fmt"

	"github.com/golang/glog"
)

func LogMetric(metric string, args ...interface{}) {
	glog.Error("$@LBSDM@$ [", metric, "] ", args)
}
func LogMetricf(metric, format string, args ...interface{}) {
	LogMetric(metric, fmt.Sprintf(format, args))
}
func LogMetricln(metric string, args ...interface{}) {
	LogMetric(metric, fmt.Sprintln(args))
}

func LogBoolStackdriverMetric(metric string, value bool, args ...interface{}) {
	LogMetric(metric, "B[", value, "] ", args)
}
func LogBoolStackdriverMetricf(metric string, value bool, format string, args ...interface{}) {
	LogBoolStackdriverMetric(metric, value, fmt.Sprintf(format, args))
}
func LogBoolStackdriverMetricln(metric string, value bool, args ...interface{}) {
	LogBoolStackdriverMetric(metric, value, fmt.Sprintln(args))
}

func LogIntStackdriverMetric(metric string, value int, args ...interface{}) {
	LogMetric(metric, "I[", value, "] ", args)
}
func LogIntStackdriverMetricf(metric string, value int, format string, args ...interface{}) {
	LogIntStackdriverMetric(metric, value, fmt.Sprintf(format, args))
}
func LogIntStackdriverMetricln(metric string, value int, args ...interface{}) {
	LogIntStackdriverMetric(metric, value, fmt.Sprintln(args))
}

func LogStringStackdriverMetric(metric, value string, args ...interface{}) {
	LogMetric(metric, "S[", value, "] ", args)
}
func LogStringStackdriverMetricf(metric, value, format string, args ...interface{}) {
	LogStringStackdriverMetric(metric, value, fmt.Sprintf(format, args))
}
func LogStringStackdriverMetricln(metric, value string, args ...interface{}) {
	LogStringStackdriverMetric(metric, value, fmt.Sprintln(args))
}

func LogCountMetric(metric string, args ...interface{}) {
	LogMetric(metric, args)
}
func LogCountMetricf(metric, format string, args ...interface{}) {
	LogCountMetric(metric, fmt.Sprintf(format, args...))
}
func LogCountMetricln(metric string, args ...interface{}) {
	LogCountMetric(metric, fmt.Sprintln(args))
}
