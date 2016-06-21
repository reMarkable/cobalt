#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Runs all of the analyzers. This file also contains utilities common to
all analyzers.
"""

import os
import sys

THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
sys.path.insert(0, ROOT_DIR)

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

def main():
  runAllAnalyzers()

if __name__ == '__main__':
  main()