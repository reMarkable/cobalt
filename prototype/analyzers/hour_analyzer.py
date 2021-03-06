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

import analyzer
import utils.file_util as file_util

class HourOfDayAnalyzer:
  """ An Analyzer that outputs RAPPOR estimates of number of reports by hour-
  of-the-day. We use *basic* RAPPOR (no Bloom filters) with 24 buckets.
  """

  def analyze(self):
    ''' Use RAPPOR analysis to output estimates for number of reports by hour-
    of-the-day.
    '''

    analyzer.analyzeUsingRAPPOR(file_util.HOUR_SHUFFLER_OUTPUT_FILE_NAME,
                                file_util.RAPPOR_HOUR_CONFIG,
                                file_util.HOUR_ANALYZER_OUTPUT_FILE_NAME,
                                metric_name="hour",
                                basic_map_file_name= \
                                    file_util.HOUR_BASIC_MAP_FILE_NAME,
                                use_basic_rappor=True)
