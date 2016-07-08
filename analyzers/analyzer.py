#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors
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

"""Runs all of the analyzers. This file also contains utilities common to
all analyzers.
"""

import os
import sys

THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
sys.path.insert(0, ROOT_DIR)

import city_analyzer
import hour_analyzer
import module_name_analyzer
import help_query_analyzer
import utils.data as data
import utils.file_util as file_util

def runAllAnalyzers():
  """Runs all of the analyzers.

  This function does not return anything but it invokes all of the
  analyzers each of which will read a file from the 's_to_a' directory
  and write a file in the top level out directory.
  """

  # Run the help query analyzer
  print "Running the help-query analyzer..."
  hq_analyzer = help_query_analyzer.HelpQueryAnalyzer()
  hq_analyzer.analyze()

  # Run the city names analyzer
  print "Running the city names analyzer..."
  cn_analyzer = city_analyzer.CityNamesAnalyzer()
  cn_analyzer.analyze()

  # Run the module names analyzer
  print "Running the module names analyzer..."
  mn_analyzer = module_name_analyzer.ModuleNameAnalyzer()
  mn_analyzer.analyze()

  # Run the hour-of-day analyzer
  print "Running the hour-of-day analyzer..."
  hd_analyzer = hour_analyzer.HourOfDayAnalyzer()
  hd_analyzer.analyze()


def main():
  runAllAnalyzers()

if __name__ == '__main__':
  main()
