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

class CityRatingsAnalyzer:
  """An Analyzer that outputs RAPPOR association estimates of average ratings
  by city.
  """
  def analyze(self):
    '''Use RAPPOR correlation analysis to output a 2-D estimate of cities and
    fraction of reports per rating.

    Use this information to compute average rating across cities.
    '''
    analyzer.analyzeUsingRAPPOR(file_util.CITY_SHUFFLER_OUTPUT_FILE_NAME,
        file_util.RAPPOR_CITY_NAME_CONFIG,
        file_util.CITY_RATINGS_ANALYZER_OUTPUT_FILE_NAME,
        metric_name='city_name_and_rating',
        candidates_file_name=file_util.CITY_CANDIDATES_FILE_NAME,
        assoc_options = {
          'config_file': file_util.RAPPOR_CITY_RATINGS_ASSOC_CONFIG,
          'metric_name': 'CityAverageRating',
          'vars': ['city_name', 'rating'],
        })
