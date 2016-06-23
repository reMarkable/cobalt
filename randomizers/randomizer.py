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

"""Runs all of the randomizers. This file also contains utilities common to
all randomizers.
"""

import csv
import os
import sys

THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
sys.path.insert(0, ROOT_DIR)

import help_query_randomizer
import city_randomizer

import utils.data as data
import utils.file_util as file_util

def runAllRandomizers(entries):
  """Runs all of the randomizers on the given list of entries.

  This function does not return anything but it invokes all of the
  randomizers each of which will write a file
  into the "r_to_s" output directory containing the randomizer output.

  Args:
    entries {list of Entry}: The entries to be randomized.
  """

  # Run the help query randomizer
  print "Running the help-query randomizer..."
  hq_randomizer = help_query_randomizer.HelpQueryRandomizer()
  hq_randomizer.randomize(entries)
  print "...done."

  # Run the city randomizer
  print "Running the city randomizer..."
  c_randomizer = city_randomizer.CityRandomizer()
  c_randomizer.randomize(entries)
  print "...done."

def main():
  entries = data.readEntries(file_util.GENERATED_INPUT_DATA_FILE_NAME)
  runAllRandomizers(entries)

if __name__ == '__main__':
  main()
