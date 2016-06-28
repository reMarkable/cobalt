#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import csv
import os
import sys

THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
sys.path.insert(0, ROOT_DIR)

import third_party.rappor.client.python.rappor as rappor

try:
  import fastrand.fastrand as fastrand
except ImportError:
  print >>sys.stderr, (
      "Native fastrand module not imported; see README for speedups")
  fastrand = None


import utils.data as data
import utils.file_util as file_util


class CityRandomizer:
  """ A Randomizer that extracts city name and rating from |Entry| and applies
  randomized response algorithms to both entries to emit noised reports.
  Additionally, it uses user_id from |Entry| to setup the secret in the RAPPOR
  encoder. (The secret is irrelevant for the purposes of this demo.)
  """

  def randomize(self, entries):
    """ A Randomizer that extracts city name and rating from |Entry| and
    applies randomized response algorithms to both entries to emit noised
    reports.

    This function does not return anything but it writes a file
    into the "r_to_s" output directory containing the randomizer output.

    Args:
      entries {list of Entry}: The entries to be randomized.
    """
    # Fastrand module written in C++ speeds up random number generation.
    if fastrand:
      print('Using fastrand extension')
      # NOTE: This doesn't take 'rand'.  It's seeded in C with srand().
      irr_rand = fastrand.FastIrrRand
    else:
      print('Warning: fastrand module not importable; see README for build '
          'instructions.  Falling back to simple randomness.')
      irr_rand = rappor.SecureIrrRand

    with file_util.openFileForReading(
      file_util.RAPPOR_CITY_NAME_CONFIG, file_util.CONFIG_DIRECTORY) as cf:
      city_name_params = rappor.Params.from_csv(cf)

    with file_util.openForRandomizerWriting(
      file_util.CITY_RANDOMIZER_OUTPUT_FILE_NAME) as f:
      writer = csv.writer(f)
      # Format string for city name RAPPOR reports.
      city_name_fmt_string = '0%ib' % city_name_params.num_bloombits
      for entry in entries:
        # user_id is used to derive a cohort.
        city_name_cohort = entry.user_id % city_name_params.num_cohorts
        # user_id is used to derive a per-client secret as required by the
        # RAPPOR encoder. For the current prototype, clients only report one
        # value, so using RAPPOR protection across multiple client values is
        # not demonstrated.
        city_name_e = rappor.Encoder(city_name_params, city_name_cohort,
                                     str(entry.user_id),
                                     irr_rand(city_name_params))
        city_name_rr = city_name_e.encode(entry.city)
        # TODO(pseudorandom, rudominer): Add a second randomized response for
        # the rating using Basic RAPPOR. (See Co. prototype design doc for
        # mode details.)
        writer.writerow(['%d' % city_name_cohort,
                         format(city_name_rr, city_name_fmt_string)])
