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

"""Runs all of the randomizers. This file also contains utilities common to
all randomizers.
"""

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

import help_query_randomizer
import city_randomizer
import module_name_randomizer
import hour_randomizer

import utils.data as data
import utils.file_util as file_util

def initializeFastrand():
  """ Initializes fastrand environment.

  Returns: A fastrand module or an object for performing simple
  randomness depending on the fastrand extension availability.
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
  return irr_rand

def readRapporConfigParams(config_file):
  """ Returns the RAPPOR config params as specified by the config file in csv
  format.

  Args:
    config_file {string}: The simple name of the RAPPOR config file.

  Returns: A list of RAPPOR configuration values.
  """
  with file_util.openFileForReading(
    config_file, file_util.CONFIG_DIR) as cf:
    return rappor.Params.from_csv(cf)

def encodeWithBloomFilter(user_id, data, config_params):
  """ Encodes plain text using RAPPOR with bloom filters using user_id to
  derive per-client secret and returns an encoded string along with the
  generated cohort value.

  Args:
    user_id {Int}: A unique value for each input entry that is used for
                   deriving client secret.
    data {string}: Data to be encoded.
    config_params: A list of RAPPOR configuration values.

  Returns: cohort value and the RAPPOR encoded string.
  """
  # initialize fastrand module
  irr_rand = initializeFastrand()

  # user_id is used to derive a cohort.
  cohort = user_id % config_params.num_cohorts

  # user_id is used to derive a per-client secret as required by the
  # RAPPOR encoder. For the current prototype, clients only report one
  # value, so using RAPPOR protection across multiple client values is
  # not demonstrated.
  data_e = rappor.Encoder(config_params, cohort,
                          str(user_id),
                          irr_rand(config_params))

  data_rr = data_e.encode(data)
  return cohort, data_rr

def encodeWithoutBloomFilter(user_id, data, config_params):
  """ Encodes plain text using basic RAPPOR (without any bloom filters)
  and returns an encoded string with cohort set to 0.

  Args:
    user_id {Int}: A unique value for each input entry that is used for
                   deriving client secret.
    data {string}: Data to be encoded.
    config_params: A list of RAPPOR configuration values.

  Returns: cohort value and the RAPPOR encoded string.
  """
  # initialize fastrand module
  irr_rand = initializeFastrand()

  # Only a single cohort for all users.
  cohort = 0

  # For simple data like hour of the day that is bounded between 0 and
  # 23, we use basic RAPPOR(no Bloom filters) with a single cohort
  # (specified with cohort=0) for all users.
  data_e = rappor.Encoder(config_params, cohort,
                          str(user_id),
                          irr_rand(config_params))

  # The data is usually a small deterministic value that is well bounded.
  # We use basic RAPPOR (no Bloom filters) and represent the value
  # |n| as a bit string with all zeroes except a 1 in position n;
  # in other words as the number 2^n.
  data_rr = data_e.encode_bits(2**data)
  return cohort, data_rr

def randomizeUsingRappor(entries, param_index, output_file, config_file,
    use_bloom_filters):
  """ A helper function that may be invoked by individual randomizers.
  It reads input data in the form of a CSV file, performs some randomization
  on data using RAPPOR with or without bloom filters, and then writes output
  to another CSV file to be consumed by a shuffler.

  Args:
    entries: A list of input entries to be randomized.

    param_index: Data to be encoded is specified by an index into |Entry| tuple.

    input_file {string}: The simple name of the input CSV file to be read from
    the 'out' directory.

    output_file {string}: The simple name of the CSV file to be written in
    the 'r_to_s' directory.

  """
  # read RAPPOR config params
  config_params = readRapporConfigParams(config_file)

  with file_util.openForRandomizerWriting(output_file) as f:
    writer = csv.writer(f)

    # Format strings for RAPPOR reports.
    config_fmt_string = '0%ib' % config_params.num_bloombits

    for entry in entries:
      if use_bloom_filters:
        (cohort, data_rr) = encodeWithBloomFilter(entry.user_id,
                                entry[param_index],
                                config_params)
      else:
        (cohort, data_rr) = encodeWithoutBloomFilter(entry.user_id,
                                entry[param_index],
                                config_params)

      writer.writerow(['%d' % cohort,
                      format(data_rr, config_fmt_string)])


def runAllRandomizers(entries):
  """Runs all of the randomizers on the given list of entries.

  This function does not return anything but it invokes all of the
  randomizers each of which will write a file
  into the "r_to_s" output directory containing the randomizer output.

  Args:
    entries {list of Entry}: The entries to be randomized.
  """

  # Run the help query randomizer
  print "Running the help-query randomizer..."
  hq_randomizer = help_query_randomizer.HelpQueryRandomizer()
  hq_randomizer.randomize(entries)

  # Run the city randomizer
  print "Running the city randomizer..."
  c_randomizer = city_randomizer.CityRandomizer()
  c_randomizer.randomize(entries)

  # Run the module name randomizer
  print "Running the module name randomizer..."
  mn_randomizer = module_name_randomizer.ModuleNameRandomizer()
  mn_randomizer.randomize(entries)

  # Run the hour randomizer
  print "Running the hour of day randomizer..."
  hr_randomizer = hour_randomizer.HourRandomizer()
  hr_randomizer.randomize(entries)

def main():
  entries = data.readEntries(file_util.GENERATED_INPUT_DATA_FILE_NAME)
  runAllRandomizers(entries)

if __name__ == '__main__':
  main()
