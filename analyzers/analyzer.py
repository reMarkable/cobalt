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
import utils.public_key_crypto_helper as crypto_helper

# Should public key encryption be used for communication between the
# Randomizers and the Analyzers via the Shufflers?
_use_public_key_encryption=False

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

  decrypt_on_analyzer=None
  if _use_public_key_encryption:
    ch = crypto_helper.CryptoHelper()
    decrypt_on_analyzer=ch.decryptOnAnalyzer

  with file_util.openForAnalyzerReading(input_file) as input_f:
    with file_util.openForWriting(output_file) as output_f:
      forculus_evaluator = forculus.ForculusEvaluator(config.threshold, input_f)
      forculus_evaluator.ComputeAndWriteResults(output_f,
          additional_decryption_func=decrypt_on_analyzer)

def runAllAnalyzers(use_public_key_encryption=False):
  """Runs all of the analyzers.

  This function does not return anything but it invokes all of the
  analyzers each of which will read a file from the 's_to_a' directory
  and write a file in the top level out directory.

  Args:
    use_public_key_encryption {boolean}: Should public key encrytpion be
    used to encrypt communication between the Randomizers and the Analyzers
    via the shufflers?
  """

  global _use_public_key_encryption
  _use_public_key_encryption = use_public_key_encryption

  print "Running the help-query analyzer..."
  hq_analyzer = help_query_analyzer.HelpQueryAnalyzer()
  hq_analyzer.analyze()

  # Note(rudominer) We don't visualize the results of this analysis so there
  # is not point running it. The CityRatingsAnalyzer below is used instead.
  # print "Running the city names analyzer..."
  # cn_analyzer = city_analyzer.CityNamesAnalyzer()
  # cn_analyzer.analyze()

  print "Running the city ratings analyzer..."
  cr_analyzer = city_analyzer.CityRatingsAnalyzer()
  cr_analyzer.analyze()

  print "Running the module names analyzer..."
  mn_analyzer = module_name_analyzer.ModuleNameAnalyzer()
  mn_analyzer.analyze()

  print("Running the module names analyzer with differentially private "
        "release...")
  mn_analyzer.analyze(for_private_release=True)

  print "Running the hour-of-day analyzer..."
  hd_analyzer = hour_analyzer.HourOfDayAnalyzer()
  hd_analyzer.analyze()

  print "Running the url analyzer..."
  u_analyzer = url_analyzer.UrlAnalyzer()
  u_analyzer.analyze()

def main():
  runAllAnalyzers()

if __name__ == '__main__':
  main()
