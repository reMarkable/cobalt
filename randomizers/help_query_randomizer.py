#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys

THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
sys.path.insert(0, ROOT_DIR)

import algorithms.forculus.forculus as forculus
import utils.data as data
import utils.file_util as file_util

# TODO(rudominer) Read  THRESHOLD from a config file.
THRESHOLD = 20

class HelpQueryRandomizer:
  """ A Randomizer that extracts the help query string from an |Entry| and uses
  Forculus threshold encryption to emit an encrypted version of it.
  """

  def randomize(self, entries):
    """ Extracts the help query string from each |Entry| in |entries| and
    uses Forculus threshold encryption to emit an encrypted version of it.

    This function does not return anything but it writes a file
    into the "r_to_s" output directory containing the randomizer output.

    Args:
      entries {list of Entry}: The entries to be randomized.
    """
    with file_util.openForRandomizerWriting(
        file_util.HELP_QUERY_RANDOMIZER_OUTPUT_FILE_NAME) as f:
      forculus_inserter = forculus.ForculusInserter(THRESHOLD, f)
      for entry in entries:
        forculus_inserter.Insert(entry.help_query)

    # TODO(rudominer) The following code does not belong in the randomizer it
    # belongs in the analyzer. We merely put it here for now in order to
    # test the pipeline. When this code is in the analyzer it will read the
    # output of the shuffler in the 's_to_a' directory and then write its
    # output into the root 'out' directory. But for testing purposes we
    # are directly reading the unshuffled file from the 'r_to_s' directory
    # and writing the output file to the same directory.
    input_file = os.path.abspath(os.path.join(file_util.R_TO_S_DIR,
        file_util.HELP_QUERY_RANDOMIZER_OUTPUT_FILE_NAME))
    with open(input_file,'rb')as input_f:
      with file_util.openForRandomizerWriting(
          "help_query_analyzer_out") as output_f:
        forculus_evaluator = forculus.ForculusEvaluator(THRESHOLD, input_f)
        forculus_evaluator.ComputeAndWriteResults(output_f)

