#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

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


class HourRandomizer:
  """ A Randomizer that extracts hour from |Entry| and applies randomized
  response algorithms to hour-of-the-day field to emit noised reports.
  Additionally, it uses user_id from |Entry| to setup the secret in the RAPPOR
  encoder. (The secret is irrelevant for the purposes of this demo.)
  """

  def randomize(self, entries):
    """ A Randomizer that extracts hour from |Entry| and applies randomized
    response algorithms to it to emit noised reports.

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
      file_util.RAPPOR_HOUR_CONFIG, file_util.CONFIG_DIRECTORY) as cf:
      hour_params = rappor.Params.from_csv(cf)

    with file_util.openForRandomizerWriting(
      file_util.HOUR_RANDOMIZER_OUTPUT_FILE_NAME) as f:
      writer = csv.writer(f)
      # Format strings for RAPPOR reports.
      hour_fmt_string = '0%ib' % hour_params.num_bloombits

      for entry in entries:
        # For hour of day there are only 24 values and so we use basic RAPPOR
        # (no Bloom filters) and so we use a single cohort for all users.
        hour_e = rappor.Encoder(hour_params, 0,
                                  str(entry.user_id),
                                  irr_rand(hour_params))

        # The hour of day value is an integer from 0 through 23.
        # We use basic RAPPOR (no Bloom filters) and represent the value
        # |n| as a bit string with all zeroes except a 1 in position n;
        # in other words as the number 2^n.
        hour_rr = hour_e.encode_bits(2**entry.hour)

        # Only a single cohort for all users.
        writer.writerow(['%d' % 1,
                         format(hour_rr, hour_fmt_string)])
