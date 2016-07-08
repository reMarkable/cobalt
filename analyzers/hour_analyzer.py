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

class HourOfDayAnalyzer:
  """ An Analyzer that outputs RAPPOR estimates of number of reports by hour-
  of-the-day. We use *basic* RAPPOR (no Bloom filters) with 24 buckets.
  """

  def analyze(self):
    ''' Use RAPPOR analysis to output estimates for number of reports by hour-
    of-the-day.
    '''
    # First get hour-of-day params.
    with file_util.openFileForReading(
      file_util.RAPPOR_HOUR_CONFIG, file_util.CONFIG_DIR) as cf:
      hour_params = rappor.Params.from_csv(cf)

    # Because we are using basic RAPPOR the map file is a static file in the
    # config directory--we do not have to generate a map file from a candidate
    # file. But we copy it into the cache directory because the R script will
    # write a .rda file in the same directory and we don't want it to write
    # into the config directory.
    map_file = os.path.join(file_util.CONFIG_DIR, file_util.HOUR_MAP_FILE_NAME)
    map_file_copy = os.path.join(
        file_util.CACHE_DIR, file_util.HOUR_MAP_FILE_NAME)
    self._copyFileIfNecessary(map_file, map_file_copy)

    # Next, compute the counts write into analyzer temp directory.
    with file_util.openForAnalyzerReading(
        file_util.HOUR_SHUFFLER_OUTPUT_FILE_NAME) as input_f:
      with file_util.openForAnalyzerTempWriting(
          file_util.HOUR_COUNTS_FILE_NAME) as output_f:
        sum_bits.sumBits(hour_params, input_f, output_f)

    # Next invoke the R script 'decode_dist.R'.

    # First we build the command string.
    rappor_decode_script = os.path.join(THIRD_PARTY_DIR,
        'rappor', 'bin', 'decode_dist.R')
    counts_file = os.path.abspath(os.path.join(file_util.ANALYZER_TMP_OUT_DIR,
        file_util.HOUR_COUNTS_FILE_NAME))
    params_file =  os.path.abspath(os.path.join(file_util.CONFIG_DIR,
        file_util.RAPPOR_HOUR_CONFIG))
    cmd = [rappor_decode_script, '--map', map_file_copy,
           '--counts', counts_file,
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
        file_util.OUT_DIR, file_util.HOUR_ANALYZER_OUTPUT_FILE_NAME))
    shutil.copyfile(src, dst)

  def _copyFileIfNecessary(self, from_file, to_file):
    ''' Copies from_file to to_file if from_file has been modified more
    recently than to_file.
    '''
    if (not os.path.exists(to_file) or
        os.path.getmtime(to_file) < os.path.getmtime(from_file) + 10):
      shutil.copyfile(from_file, to_file)


