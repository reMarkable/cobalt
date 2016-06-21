#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import algorithms.forculus.forculus as forculus
import utils.file_util as file_util

# TODO(rudominer) Read  THRESHOLD from a config file.
THRESHOLD = 20

class HelpQueryAnalyzer:
  """ An Analyzer that decrypts the data that was encrypted using Forculus
  threshold encryption in the HelpQueryRandomizer
  """

  def analyze(self):
    ''' Uses Forculus to decrypt the those entries that occur more than
    |THRESHOLD| times.
    '''
    with file_util.openForAnalyzerReading(
        file_util.HELP_QUERY_SHUFFLER_OUTPUT_FILE_NAME) as input_f:
      with file_util.openForWriting(
          file_util.HELP_QUERY_ANALYZER_OUTPUT_FILE_NAME) as output_f:
        forculus_evaluator = forculus.ForculusEvaluator(THRESHOLD, input_f)
        forculus_evaluator.ComputeAndWriteResults(output_f)