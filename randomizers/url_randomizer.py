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

import randomizer
import utils.file_util as file_util

class UrlRandomizer:
  """ A Randomizer that extracts the help query string from an |Entry| and uses
  Forculus threshold encryption to emit an encrypted version of it.
  """

  def randomize(self, entries):
    """ Extracts the url string from each |Entry| in |entries| and
    uses Forculus threshold encryption to emit an encrypted version of it.

    This function does not return anything but it writes a file
    into the "r_to_s" output directory containing the randomizer output.

    Args:
      entries {list of Entry}: The entries to be randomized.
    """
    randomizer.randomizeUsingForculus(entries,
        6, # url index in |Entry| tuple
        file_util.FORCULUS_URL_CONFIG,
        file_util.URL_RANDOMIZER_OUTPUT_FILE_NAME
        );
