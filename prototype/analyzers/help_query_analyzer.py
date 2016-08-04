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

import csv

import analyzer
import utils.file_util as file_util

class HelpQueryAnalyzer:
  """ An Analyzer that decrypts the data that was encrypted using Forculus
  threshold encryption in the HelpQueryRandomizer
  """

  def analyze(self):
    ''' Uses Forculus to decrypt those entries that occur more than
    |threshold| times, where |threshold| is read from the config file.
    '''
    analyzer.analyzeUsingForculus(
        file_util.HELP_QUERY_SHUFFLER_OUTPUT_FILE_NAME,
        file_util.FORCULUS_HELP_QUERY_CONFIG,
        file_util.HELP_QUERY_ANALYZER_OUTPUT_FILE_NAME);
