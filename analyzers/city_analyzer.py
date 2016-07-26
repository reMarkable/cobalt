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
import shutil
import subprocess

import algorithms.rappor.sum_bits as sum_bits
import utils.file_util as file_util
import third_party.rappor.client.python.rappor as rappor
import third_party.rappor.bin.hash_candidates as hash_candidates

THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
THIRD_PARTY_DIR = os.path.join(ROOT_DIR, "third_party")
ALGORITHMS_DIR = os.path.join(ROOT_DIR, "algorithms")

_logger = logging.getLogger()

def _generateMapFileIfNecessary(map_file, params):
  ''' Generates the map file in the cache directory if there is not already
  an up-to-date version.
  '''
  candidate_file = os.path.join(file_util.CONFIG_DIR,
      file_util.CITY_CANDIDATES_FILE_NAME)

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


class CityRatingsAnalyzer:
  """An Analyzer that outputs RAPPOR association estimates of average ratings
  by city.
  """
  def analyze(self):
    '''Use RAPPOR correlation analysis to output a 2-D estimate of cities and
    fraction of reports per rating.

    Use this information to compute average rating across cities.
    '''
    # First get city names params.
    with file_util.openFileForReading(
      file_util.RAPPOR_CITY_NAME_CONFIG, file_util.CONFIG_DIR) as cf:
      city_name_params = rappor.Params.from_csv(cf)

    map_file = os.path.join(file_util.CACHE_DIR, file_util.CITY_MAP_FILE_NAME)

    _generateMapFileIfNecessary(map_file, city_name_params)

    # Create the temp directory.
    file_util.ensureDir(file_util.ANALYZER_TMP_OUT_DIR)

    # Next invoke the R script 'decode_assoc_averages.R'.
    # First we build the command string.
    rappor_avg_decode_script = os.path.join(ALGORITHMS_DIR,
        'rappor', 'decode_assoc_averages.R')
    schema_file = os.path.abspath(os.path.join(
                    file_util.CONFIG_DIR,
                    file_util.RAPPOR_CITY_RATINGS_ASSOC_CONFIG))
    reports_file = os.path.abspath(os.path.join(
                    file_util.S_TO_A_DIR,
                    file_util.CITY_SHUFFLER_OUTPUT_FILE_NAME))
    em_executable_file = os.path.abspath(os.path.join(THIRD_PARTY_DIR,
                                            'rappor', 'analysis',
                                            'cpp', '_tmp', 'fast_em'))
    if os.path.isfile(em_executable_file) == False:
      exception_str = 'Expect fast_em executable at %s.' % em_executable_file
      exception_str += (' Terminating CityRatingsAnalyzer.'
                        ' Please run ./cobalt.py build to build'
                        ' fast_em executable.\n')

      raise Exception('\n****' + exception_str)

    cmd = [rappor_avg_decode_script,
           '--metric-name', 'CityAverageRating',
           '--schema', schema_file,
           '--reports', reports_file,
           '--params-dir', file_util.CONFIG_DIR,
           '--var1', 'city_name',
           '--var2', 'rating',
           '--map1', map_file,
           '--create-cat-map',
           '--max-em-iters', "1000",
           '--num-cores', "2",
           '--output-dir', file_util.ANALYZER_TMP_OUT_DIR,
           '--em-executable', em_executable_file
           ]

    # Then we change into the algorithms directory and execute the command.
    savedir = os.getcwd()
    os.chdir(ALGORITHMS_DIR)
    os.environ["RAPPOR_REPO"] = os.path.join(THIRD_PARTY_DIR, 'rappor/')
    # We supress the output from the R script unless it fails.
    try:
      subprocess.check_output(cmd, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
      print "\n**** Error running RAPPOR association decode script:"
      print e.output
      raise Exception('Fatal error. Cobalt pipeline terminating.')
    os.chdir(savedir)

    # Finally we copy the results file to the out directory
    src = os.path.abspath(os.path.join(
              file_util.ANALYZER_TMP_OUT_DIR, 'assoc-results.csv'))
    dst = os.path.abspath(os.path.join(
              file_util.OUT_DIR,
              file_util.CITY_RATINGS_ANALYZER_OUTPUT_FILE_NAME))
    shutil.copyfile(src, dst)


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

    map_file = os.path.join(file_util.CACHE_DIR, file_util.CITY_MAP_FILE_NAME)

    _generateMapFileIfNecessary(map_file, city_name_params)

    # Next, compute counts per cohort and write into analyzer temp directory.
    with file_util.openForAnalyzerReading(
        file_util.CITY_SHUFFLER_OUTPUT_FILE_NAME) as input_f:
      with file_util.openForAnalyzerTempWriting(
          file_util.CITY_NAME_COUNTS_FILE_NAME) as output_f:
        sum_bits.sumBits(city_name_params, input_f, output_f)

    # Next invoke the R script 'decode_dist.R'.

    # First we build the command string.
    rappor_decode_script = os.path.join(THIRD_PARTY_DIR,
        'rappor', 'bin', 'decode_dist.R')
    counts_file = os.path.abspath(os.path.join(file_util.ANALYZER_TMP_OUT_DIR,
        file_util.CITY_NAME_COUNTS_FILE_NAME))
    params_file =  os.path.abspath(os.path.join(file_util.CONFIG_DIR,
        file_util.RAPPOR_CITY_NAME_CONFIG))
    cmd = [rappor_decode_script, '--map', map_file, '--counts', counts_file,
           '--params', params_file, '--output-dir',
           file_util.ANALYZER_TMP_OUT_DIR]

    # Then we change into the Rappor directory and execute the command.
    savedir = os.getcwd()
    os.chdir(os.path.join(THIRD_PARTY_DIR, 'rappor'))
    # We supress the output from the R script unless it fails.
    try:
      subprocess.check_output(cmd, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
      print "\n**** Error running RAPPOR decode script:"
      print e.output
      raise Exception('Fatal error. Cobalt pipeline terminating.')
    os.chdir(savedir)

    # Finally we copy the results file to the out directory
    src = os.path.abspath(os.path.join(
        file_util.ANALYZER_TMP_OUT_DIR, 'results.csv'))
    dst = os.path.abspath(os.path.join(
        file_util.OUT_DIR, file_util.CITY_NAMES_ANALYZER_OUTPUT_FILE_NAME))
    shutil.copyfile(src, dst)
