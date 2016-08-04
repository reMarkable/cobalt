#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import randomizer
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
    randomizer.randomizeUsingRappor(entries,
        [(2, # city name index in |Entry|
          True, # use bloom filters
          file_util.RAPPOR_CITY_NAME_CONFIG # rappor config file name
         ),
         (4, # rating index in |Entry|
          False, # no bloom filters
          file_util.RAPPOR_RATING_CONFIG # rappor config file name
          )
        ],
        file_util.CITY_RANDOMIZER_OUTPUT_FILE_NAME,
        );
