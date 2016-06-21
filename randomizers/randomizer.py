#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

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

def main():
  entries = data.readEntries(file_util.GENERATED_INPUT_DATA_FILE_NAME)
  runAllRandomizers(entries)

if __name__ == '__main__':
  main()