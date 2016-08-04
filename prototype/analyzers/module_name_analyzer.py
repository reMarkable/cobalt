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
    config_file = (file_util.RAPPOR_MODULE_NAME_PR_CONFIG if
        for_private_release else file_util.RAPPOR_MODULE_NAME_CONFIG)
    map_file_name = (file_util.MODULE_PR_MAP_FILE_NAME if
        for_private_release else file_util.MODULE_MAP_FILE_NAME)
    counts_file_name = file_util.MODULE_NAME_COUNTS_FILE_NAME
    candidates_file_name = file_util.MODULE_CANDIDATES_FILE_NAME
    input_file = (file_util.MODULE_NAME_PR_SHUFFLER_OUTPUT_FILE_NAME if
      for_private_release else
      file_util.MODULE_NAME_SHUFFLER_OUTPUT_FILE_NAME)
    output_file = (file_util.MODULE_NAME_PR_ANALYZER_OUTPUT_FILE_NAME if
      for_private_release else
      file_util.MODULE_NAME_ANALYZER_OUTPUT_FILE_NAME)

    rappor_files = {
        'input_file': input_file,
        'config_file': config_file,
        'map_file_name': map_file_name,
        'counts_file_name': counts_file_name,
        'output_file': output_file,
        'candidates_file_name': candidates_file_name,
        }

    analyzer.analyzeUsingRAPPOR(rappor_files, for_private_release=for_private_release)
