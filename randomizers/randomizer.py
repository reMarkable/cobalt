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

import algorithms.forculus.forculus as forculus
import city_randomizer
import help_query_randomizer
import hour_randomizer
import module_name_randomizer
import url_randomizer
import utils.data as data
import utils.file_util as file_util
import utils.public_key_crypto_helper as crypto_helper

# Should public key encryption be used for communication between the
# Randomizers and the Analyzers via the Shufflers?
_use_public_key_encryption=False

def initializeFastrand():
  ''' Initializes fastrand environment.

  Returns: A fastrand module or an object for performing simple
  randomness depending on the fastrand extension availability.
  '''
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

def readRapporConfigParamsFromFile(config_file):
  ''' Returns the RAPPOR config params as specified by the config file in csv
  format.

  Args:
    config_file {string}: The simple name of the RAPPOR config file.

  Returns: A list of RAPPOR configuration values.
  '''
  with file_util.openFileForReading(
    config_file, file_util.CONFIG_DIR) as cf:
    return rappor.Params.from_csv(cf)

def encodeWithBloomFilter(user_id, data, config_params, irr_rand):
  ''' Encodes plain text using RAPPOR with bloom filters using user_id to
  derive per-client secret and returns an encoded string along with the
  generated cohort value.

  Args:
    user_id {Int}: A unique value for each input entry that is used for
                   deriving client secret.

    data {string}: Data to be encoded.

    config_params: A list of RAPPOR configuration values.
    irr_rand {function} A function that complies with the signature of
      rappor.SecureIrrRand

  Returns: cohort value and the RAPPOR encoded string.
  '''

  # User_id is used to derive a cohort.
  cohort = user_id % config_params.num_cohorts

  # User_id is used to derive a per-client secret as required by the
  # RAPPOR encoder. For the current prototype, clients only report one
  # value, so using RAPPOR protection across multiple client values is
  # not demonstrated.
  data_e = rappor.Encoder(config_params, cohort,
                          str(user_id),
                          irr_rand(config_params))

  data_rr = data_e.encode(data)
  return cohort, data_rr

def encodeWithoutBloomFilter(user_id, data, config_params, irr_rand):
  ''' Encodes plain text using basic RAPPOR (without any bloom filters)
  and returns an encoded string with cohort set to 0.

  Args:
    user_id {Int}: A unique value for each input entry that is used for
                   deriving client secret.

    data {string}: Data to be encoded.

    config_params: A list of RAPPOR configuration values.
    irr_rand {function} A function that complies with the signature of
      rappor.SecureIrrRand

  Returns: cohort value and the RAPPOR encoded string.
  '''
  # Using a single cohort for all users.
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

def randomizeUsingRappor(entries, param_configs, output_file):
  ''' A helper function that may be invoked by individual randomizers.
  It reads input data in the form of a CSV file, performs some randomization
  on data using RAPPOR with or without bloom filters, and then writes output
  to another CSV file to be consumed by a shuffler.

  Args:
    entries: A list of input entries to be randomized.

    param_configs: A list of tuples containing a param index into |Entry| tuple,
    a boolean value to specify whether that param supports cohort based analysis
    or not, and the name of the RAPPOR config_file for the specified param.
    For example:
       param_configs = [(1, True, 'rappor_module_name_config.csv')]
    implies that the randomization should be performed on module_names using
    bloom filters and RAPPOR configuration as specified in file:
    <rappor_module_name_config.csv>.

    output_file {string}: The simple name of the CSV file to be written in
    the 'r_to_s' directory.
  '''
  with file_util.openForRandomizerWriting(output_file) as f:
    writer = csv.writer(f)

    # Read RAPPOR config params into a list of config params with keys as
    # param index from |Entry|.
    rappor_configs = {}
    for config in param_configs:
      config_params = readRapporConfigParamsFromFile(config[2])
      config_fmt_string = '0%ib' % config_params.num_bloombits
      rappor_configs[config[0]] = (config_params, config_fmt_string)

    # Initialize fastrand module
    irr_rand = initializeFastrand()

    encrypt_for_analyzer=None
    if _use_public_key_encryption:
      ch = crypto_helper.CryptoHelper()
      encrypt_for_analyzer = ch.encryptForSendingToAnalyzer

    # Format strings for RAPPOR reports.
    for entry in entries:
      # For randomizing multiple params, generate the encoded data based on
      # cohort configuration for each param separately.
      data_out = []
      data_out.append('%d' % 0) # default cohort
      for param_index, use_bloom_filter, config_file in param_configs:
        if use_bloom_filter:
          (cohort, data_rr) = encodeWithBloomFilter(entry.user_id,
                                  entry[param_index],
                                  rappor_configs[param_index][0],
                                  irr_rand)
          # For now, always use the cohort generated from the bloom filter, if
          # multiple params are involved.
          # TODO(ukode): From an API perspective, it might be best to have
          # (cohort, report) separate for each metric we report and just treat
          # the cohort as redundant if it's boolean/basic RAPPOR. If we ever
          # choose to use different # of cohorts for different metrics, this
          # will be useful in the future.
          data_out[0] = '%d' % cohort
        else:
          (cohort, data_rr) = encodeWithoutBloomFilter(entry.user_id,
                                  entry[param_index],
                                  rappor_configs[param_index][0],
                                  irr_rand)
        data_out.append(format(data_rr, rappor_configs[param_index][1]))

      # Write each row to the out file with the following syntax:
      # {cohort, data1_rr, data2_rr, data3_rr, ...}
      # for example, city_rating_randomized output looks like:
      # {cohort, city_name_rr, rating_rr)
      data_to_write = [data for data in data_out]
      if _use_public_key_encryption:
        # Express the data-to-write as a single string with comma-separated
        # fields. Then encrypt that string, receiving a tuple of strings
        # representing the ciphertext. We use that tuple as the data-to-write.
        data_to_write = encrypt_for_analyzer(",".join(map(str, data_to_write)))
      writer.writerow(data_to_write)

def randomizeUsingForculus(entries, param_index, config_file, output_file):
  '''A helper function that may be invoked by individual randomizers.
  It reads input data in the form of a CSV file, performs some randomization
  on data using Forculus, and then writes output to another CSV file to be
  consumed by a shuffler.

  Args:
    entries: A list of input entries to be randomized.

    param_index {int}: An index into |Entry| tuple that identifies the
    parameter to be randomized.

    config_file {string}: The simple name of the Forculus config file used for
    randomizing the param specified by |param_index|.

    output_file {string}: The simple name of the CSV file to be written in
    the 'r_to_s' directory.
  '''
  with file_util.openFileForReading(config_file, file_util.CONFIG_DIR) as cf:
    config = forculus.Config.from_csv(cf)

  encrypt_for_analyzer = None
  if _use_public_key_encryption:
    ch = crypto_helper.CryptoHelper()
    encrypt_for_analyzer=ch.encryptForSendingToAnalyzer

  with file_util.openForRandomizerWriting(output_file) as f:
    forculus_inserter = forculus.ForculusInserter(config.threshold, f)
    for entry in entries:
      forculus_inserter.Insert(entry[param_index],
          additional_encryption_func=encrypt_for_analyzer)

def runAllRandomizers(entries, use_public_key_encryption=False):
  '''Runs all of the randomizers on the given list of entries.

  This function does not return anything but it invokes all of the
  randomizers each of which will write a file
  into the "r_to_s" output directory containing the randomizer output.

  Args:
    entries {list of Entry}: The entries to be randomized.
    use_public_key_encryption {boolean}: Should public key encrytpion be
    used to encrypt communication between the Randomizers and the Analyzers
    via the shufflers?
  '''

  global _use_public_key_encryption
  _use_public_key_encryption = use_public_key_encryption

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

   # Run the module name randomizer in another mode
  print("Running the module name randomizer for differentially "
      "private release...")
  mn_randomizer.randomize(entries, for_private_release=True)

  # Run the hour randomizer
  print "Running the hour of day randomizer..."
  hr_randomizer = hour_randomizer.HourRandomizer()
  hr_randomizer.randomize(entries)

  # Run the url randomizer
  print "Running the url randomizer..."
  u_randomizer = url_randomizer.UrlRandomizer()
  u_randomizer.randomize(entries)

def readAndRandomize(use_public_key_encryption=False):
  '''Reads the fake data and runs all of the Randomizers on it.

  Args:
    use_public_key_encryption {boolean}: Should public key encrytpion be
    used to encrypt communication between the Randomizers and the Analyzers
    via the shufflers?
  '''
  entries = data.readEntries(file_util.GENERATED_INPUT_DATA_FILE_NAME)
  runAllRandomizers(entries, use_public_key_encryption)

def main():
  readAndRandomize()

if __name__ == '__main__':
  main()
