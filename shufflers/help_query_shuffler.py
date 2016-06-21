#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


import shuffler
import utils.file_util as file_util

class HelpQueryShuffler:
  """ A shuffler for the help query pipeline
  """

  def shuffle(self):
    ''' This function invokes the generic function shuffleCSVFiles() on
    the help-query-specific CSV files.
    '''
    shuffler.shuffleCSVFiles(file_util.HELP_QUERY_RANDOMIZER_OUTPUT_FILE_NAME,
        file_util.HELP_QUERY_SHUFFLER_OUTPUT_FILE_NAME)
