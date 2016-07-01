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

"""This script is part of an end-to-end test for the Cobalt prototype.
It should be invoked via 'cobalt.py test'. This will run the full
pipeline and then invoke this script to check the results.

See the individual _check_*() functions for an explanation of what
each one is checking.
"""

import csv
import os
import sys

THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir,
  os.path.pardir))
sys.path.insert(0, ROOT_DIR)

import algorithms.forculus.forculus as forculus
import utils.file_util as file_util


def check_results():
  """Checks the results of running the Cobalt prototype pipeline.
  """
  _check_help_query_results()

def _check_help_query_results():
  """ Checks the results of the Cobalt help-query pipeline. This pipeline
  exracts the help queries from the input data and uses Forculus threshold
  encryption to encrypt them and then decrypt the ones that occur at least
  |threshold| times, where |threshold| is a number read from a config file.

  The straight counting pipline extracts the help queries and then accumulates
  all of them in plain text regardless of how frequently they occur.

  In this test we compare the results of the Cobalt pipeline against the
  results of the straight-counting pipeline. The Cobalt results should be
  equal to those elements of the straight-counting results with counts
  at least |threshold|.
  """

  print "\nEnd-to-end test: Checking help-query-results."
  with file_util.openFileForReading(
      file_util.FORCULUS_HELP_QUERY_CONFIG, file_util.CONFIG_DIR) as cf:
    config = forculus.Config.from_csv(cf)

  # Read the output of the straight-counting pipeline from the csv file and
  # put into a dictionary all entries with count >= config.threshold
  # The dictionary will map help queries to their counts.
  with file_util.openForReading(
      file_util.POPULAR_HELP_QUERIES_CSV_FILE_NAME) as csvfile:
    reader = csv.reader(csvfile)
    straight_counting_results = {row[0] : int(row[1])
        for row in reader if int(row[1]) >= config.threshold}

  # Read the output of the Cobalt prototype pipeline from the csv file and
  # put all entries into a dictionary. The dictionary will map help queries to
  # their counts.
  with file_util.openForReading(
      file_util.HELP_QUERY_ANALYZER_OUTPUT_FILE_NAME) as csvfile:
    reader = csv.reader(csvfile)
    cobalt_prototype_results = {row[0] : int(row[1]) for row in reader}

  # Check that the two dictionries are equal.
  if straight_counting_results == cobalt_prototype_results:
    print "PASS"
  else:
    print "**** TEST FAILURE ****"
    a = set(straight_counting_results.items())
    b = set(cobalt_prototype_results.items())
    print "straight-counting minus Cobalt:",  a - b
    print "Cobalt minus straight-counting:", b - a



