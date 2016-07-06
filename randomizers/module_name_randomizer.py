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
import logging
import os
import sys

THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
sys.path.insert(0, ROOT_DIR)

_logger = logging.getLogger()

import third_party.rappor.client.python.rappor as rappor

try:
  import third_party.fastrand.fastrand as fastrand
except ImportError:
  fastrand = None

import utils.data as data
import utils.file_util as file_util


class ModuleNameRandomizer:
  """ A Randomizer that extracts module name from |Entry| and applies
  randomized response algorithms to it to emit noised reports. Additionally,
  it uses user_id from |Entry| to setup the secret in the RAPPOR encoder.
  (The secret is irrelevant for the purposes of this demo.)
  """

  def randomize(self, entries):
    """ A Randomizer that extracts module name from |Entry| and applies
    randomized response algorithms to it to emit noised reports.

    This function does not return anything but it writes a file
    into the "r_to_s" output directory containing the randomizer output.

    Args:
      entries {list of Entry}: The entries to be randomized.
    """
    # Fastrand module written in C++ speeds up random number generation.
    if fastrand:
      _logger.info('Using fastrand extension')
      # NOTE: This doesn't take 'rand'.  It's seeded in C with srand().
      irr_rand = fastrand.FastIrrRand
    else:
      _logger.warning('fastrand module not importable; see README for build '
          'instructions.  Falling back to simple randomness.')
      irr_rand = rappor.SecureIrrRand

    with file_util.openFileForReading(
      file_util.RAPPOR_MODULE_NAME_CONFIG, file_util.CONFIG_DIR) as cf:
      module_name_params = rappor.Params.from_csv(cf)

    with file_util.openForRandomizerWriting(
      file_util.MODULE_NAME_RANDOMIZER_OUTPUT_FILE_NAME) as f:
      writer = csv.writer(f)
      # Format strings for RAPPOR reports.
      module_name_fmt_string = '0%ib' % module_name_params.num_bloombits
      for entry in entries:
        # user_id is used to derive a cohort.
        module_name_cohort = entry.user_id % module_name_params.num_cohorts
        # user_id is used to derive a per-client secret as required by the
        # RAPPOR encoder. For the current prototype, clients only report one
        # value, so using RAPPOR protection across multiple client values is
        # not demonstrated.
        module_name_e = rappor.Encoder(module_name_params, module_name_cohort,
                                     str(entry.user_id),
                                     irr_rand(module_name_params))

        module_name_rr = module_name_e.encode(entry.name)

        writer.writerow(['%d' % module_name_cohort,
                         format(module_name_rr, module_name_fmt_string)])
