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

import logging
import os

import utils.file_util as file_util
import utils.rappor_sum_bits as sum_bits
import third_party.rappor.client.python.rappor as rappor
import third_party.rappor.bin.hash_candidates as hash_candidates

_logger = logging.getLogger()

class CityNamesAnalyzer:
  """ An Analyzer that outputs RAPPOR estimates of number of reports by city
  name.
  """

  def analyze(self):
    ''' Use RAPPOR analysis to output estimates for number of reports by city
    name.
    '''
    # First get city names params.
    with file_util.openFileForReading(
      file_util.RAPPOR_CITY_NAME_CONFIG, file_util.CONFIG_DIR) as cf:
      city_name_params = rappor.Params.from_csv(cf)

    self._generateMapFileIfNecessary(city_name_params)
    #TODO(rudominer) We also need to consider the caching of the .rda file.

    # Next, compute counts per cohort in to analyzer temp directory.
    with file_util.openForAnalyzerReading(
        file_util.CITY_SHUFFLER_OUTPUT_FILE_NAME) as input_f:
      with file_util.openForAnalyzerTempWriting(
          file_util.CITY_NAME_COUNTS_FILE_NAME) as output_f:

        sum_bits.sumBits(city_name_params, input_f, output_f)
    # TODO(pseudorandom, rudominer): Run third_party/rappor/bin/decode_dist.R
    # on inputs
    # --map = file_util.CITY_MAP_FILE_NAME
    # --counts = file_util.CITY_NAME_COUNTS_FILE_NAME
    # --params = file_util.RAPPOR_CITY_NAME_CONFIG
    # --output-dir = <this should be either analyzer temp out or |out|>
    # then pull out results from the RAPPOR output directory (results.csv)

  def _generateMapFileIfNecessary(self, params):
    ''' Generates the map file in the cache directory if there is not already
    an up-to-date version.
    '''
    candidate_file = os.path.join(file_util.CONFIG_DIR,
        file_util.CITY_CANDIDATES_FILE_NAME)

    map_file = os.path.join(file_util.CACHE_DIR,
        file_util.CITY_MAP_FILE_NAME)

    # Generate the map file unless it already exists and has a modified
    # timestamp later than that of the candidate file. We allow a buffer of
    # 10 seconds in case the timestamps are out of sync for some reason.
    if (not os.path.exists(map_file) or
        os.path.getmtime(map_file) < os.path.getmtime(candidate_file) + 10):
      with file_util.openFileForReading(file_util.CITY_CANDIDATES_FILE_NAME,
          file_util.CONFIG_DIR) as cand_f:
        with file_util.openFileForWriting(file_util.CITY_MAP_FILE_NAME,
            file_util.CACHE_DIR) as map_f:
          _logger.debug('Generating a RAPPOR map file at %s based on '
                        'candidate file at %s' % (map_file, candidate_file))
          hash_candidates.HashCandidates(params, cand_f, map_f)


