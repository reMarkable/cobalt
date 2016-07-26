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

_logger = logging.getLogger()

class ModuleNameAnalyzer:
  """ An Analyzer that outputs RAPPOR estimates of number of reports by module
  name.
  """

  def analyze(self, for_private_release=False):
    ''' Use RAPPOR analysis to output estimates for number of reports by module
    name.

    Args:
      for_private_release {bool}: If True then in addition to doing the RAPPOR
      decoding, Laplace noise is also added to the analyzed output. We use
      different input, output and config files in this case.
    '''
    # TODO(rudominer) Implement the differentially private release using
    # Laplace noise.
    config_file = (file_util.RAPPOR_MODULE_NAME_PR_CONFIG if
        for_private_release else file_util.RAPPOR_MODULE_NAME_CONFIG)
    map_file_name = (file_util.MODULE_PR_MAP_FILE_NAME if
        for_private_release else file_util.MODULE_MAP_FILE_NAME)
    input_file = (file_util.MODULE_NAME_PR_SHUFFLER_OUTPUT_FILE_NAME if
      for_private_release else
      file_util.MODULE_NAME_SHUFFLER_OUTPUT_FILE_NAME)
    output_file = (file_util.MODULE_NAME_PR_ANALYZER_OUTPUT_FILE_NAME if
      for_private_release else
      file_util.MODULE_NAME_ANALYZER_OUTPUT_FILE_NAME)

    # First get module names params.
    with file_util.openFileForReading(
      config_file, file_util.CONFIG_DIR) as cf:
      module_name_params = rappor.Params.from_csv(cf)

    map_file = os.path.join(file_util.CACHE_DIR, map_file_name)

    self._generateMapFileIfNecessary(map_file, map_file_name,
                                     module_name_params)

    # Next, compute counts per cohort and write into analyzer temp directory.
    with file_util.openForAnalyzerReading(input_file) as input_f:
      with file_util.openForAnalyzerTempWriting(
          file_util.MODULE_NAME_COUNTS_FILE_NAME) as output_f:
        sum_bits.sumBits(module_name_params, input_f, output_f)

    # Next invoke the R script 'decode_dist.R'.

    # First we build the command string.
    rappor_decode_script = os.path.join(THIRD_PARTY_DIR,
        'rappor', 'bin', 'decode_dist.R')
    counts_file = os.path.abspath(os.path.join(file_util.ANALYZER_TMP_OUT_DIR,
        file_util.MODULE_NAME_COUNTS_FILE_NAME))
    params_file =  os.path.abspath(os.path.join(file_util.CONFIG_DIR,
        config_file))
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
        file_util.OUT_DIR, output_file))
    shutil.copyfile(src, dst)

  def _generateMapFileIfNecessary(self, map_file, map_file_name, params):
    ''' Generates the map file in the cache directory if there is not already
    an up-to-date version.
    '''
    candidate_file = os.path.join(file_util.CONFIG_DIR,
        file_util.MODULE_CANDIDATES_FILE_NAME)

    # Generate the map file unless it already exists and has a modified
    # timestamp later than that of the candidate file. We allow a buffer of
    # 10 seconds in case the timestamps are out of sync for some reason.
    if (not os.path.exists(map_file) or
        os.path.getmtime(map_file) < os.path.getmtime(candidate_file) + 10):
      with file_util.openFileForReading(file_util.MODULE_CANDIDATES_FILE_NAME,
          file_util.CONFIG_DIR) as cand_f:
        with file_util.openFileForWriting(map_file_name,
            file_util.CACHE_DIR) as map_f:
          _logger.debug('Generating a RAPPOR map file at %s based on '
                        'candidate file at %s' % (map_file, candidate_file))
          hash_candidates.HashCandidates(params, cand_f, map_f)


