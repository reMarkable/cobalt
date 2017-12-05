#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

""" This script is used as part of the Cobalt demo. It provides a command-line
    interface that allows an operator to request the generation of the two
    example reports. Report 1 is for the Forculus / URL example. The report
    is presented as a table of numbers. Report 2 is the Basic RAPPOR / hour-of-
    the-day example. In addition to generating a table of numbers a
    column-chart is also generated for visualization.

    This script assumes a particular contents of the Cobalt registration system.
    It must be kept in sync with the registration files in
    <source root>/config/demo. Here we include a copy of the relevant parts of
    those files for reference:

    #### ReportConfig (1, 1, 1)
    element {
      customer_id: 1
      project_id: 1
      id: 1
      name: "Fuchsia Popular URLs"
      description: "A fictional report used for the development of Cobalt."
      metric_id: 1
      variable {
        metric_part: "url"
      }
      scheduling {
        report_finalization_days: 1
        aggregation_epoch_type: DAY
      }
    }

    #### ReportConfig (1, 1, 2)
    element {
      customer_id: 1
      project_id: 1
      id: 2
      name: "Fuchsia Usage by Hour"
      description: "A fictional report used for the development of Cobalt."
      metric_id: 2
      variable {
        metric_part: "hour"
      }
      scheduling {
        report_finalization_days: 5
        aggregation_epoch_type: WEEK
      }
    }
"""

import os
import subprocess
import sys

import generate_viz

DEMO_DIR = os.path.abspath(os.path.dirname(__file__))
ROOT_DIR = os.path.abspath(os.path.join(DEMO_DIR, os.path.pardir, os.path.pardir))
OUT_DIR =  os.path.join(ROOT_DIR,'out')

def runForculusDemo():
  path =os.path.join(OUT_DIR, 'tools', 'report_client')
  return_code =subprocess.call([path,
      "-report_master_uri", "localhost:7001",
      "-interactive=false",
      "-report_config_id=1",
      "-include_std_err_column=false",
      "-logtostderr", "-v=3"])
  if return_code < 0:
    print
    print "****** WARNING Process terminated by signal %d" % (- return_code)

def runBasicRapporDemo():
  path =os.path.join(OUT_DIR, 'tools', 'report_client')
  return_code =subprocess.call([path,
      "-report_master_uri", "localhost:7001",
      "-interactive=false",
      "-report_config_id=2",
      "-include_std_err_column=true",
      "-csv_file=%s" % generate_viz.USAGE_BY_HOUR_CSV_FILE,
      "-logtostderr", "-v=3"])
  if return_code < 0:
    print
    print "****** WARNING Process terminated by signal %d" % (- return_code)
  # Gemerate the vizualization
  generate_viz.generateViz()

def main():
  while True:
    print "Cobalt Demo"
    print "----------"
    print "1) Run Forculus report demo"
    print
    print "2) Run Basic RAPPOR report demo"
    print
    print "3) Quit"
    print
    print "Enter 1, 2 or 3"
    line = sys.stdin.readline()
    if line == '1\n':
      runForculusDemo()
    elif line == '2\n':
      runBasicRapporDemo()
    elif line == '3\n':
      break

if __name__ == '__main__':
  main()
