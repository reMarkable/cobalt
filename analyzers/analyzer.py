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

import algorithms.forculus.forculus as forculus
import city_analyzer
import hour_analyzer
import module_name_analyzer
import help_query_analyzer
import url_analyzer
import utils.data as data
import utils.file_util as file_util

def analyzeUsingForculus(input_file, config_file, output_file):
  ''' A helper function that may be invoked by individual analyzers. It reads
  input data in the form of a CSV file (that is generated as output from the
  preceeding shuffle operation in the pipeline), analyzes the data using
  Forculus, and then writes output to another CSV file to be consumed by the
  visualizer at the end of the pipeline.

  It uses Forculus to decrypt those entries that occur more than |threshold|
  times, where |threshold| is read from the config file.

  Args:
    input_file {string}: The simple name of the CSV file to be read from
    the 's_to_a' directory.

    config_file {string}: The simple name of the Forculus config file used
    for analyzing the data from the input file.

    output_file {string}: The simple name of the CSV file to be written in
    the 'out' directory, that is consumed by the visualizer.
  '''
  with file_util.openFileForReading(config_file, file_util.CONFIG_DIR) as cf:
    config = forculus.Config.from_csv(cf)

  with file_util.openForAnalyzerReading(input_file) as input_f:
    with file_util.openForWriting(output_file) as output_f:
      forculus_evaluator = forculus.ForculusEvaluator(config.threshold, input_f)
      forculus_evaluator.ComputeAndWriteResults(output_f)

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

  # Run the city ratings analyzer
  print "Running the city ratings analyzer..."
  cr_analyzer = city_analyzer.CityRatingsAnalyzer()
  try:
    cr_analyzer.analyze()
  except Exception as e:
    print e

  # Run the module names analyzer
  print "Running the module names analyzer..."
  mn_analyzer = module_name_analyzer.ModuleNameAnalyzer()
  mn_analyzer.analyze()

  # Run the hour-of-day analyzer
  print "Running the hour-of-day analyzer..."
  hd_analyzer = hour_analyzer.HourOfDayAnalyzer()
  hd_analyzer.analyze()

  # Run the url analyzer
  print "Running the url analyzer..."
  u_analyzer = url_analyzer.UrlAnalyzer()
  u_analyzer.analyze()


def main():
  runAllAnalyzers()

if __name__ == '__main__':
  main()
