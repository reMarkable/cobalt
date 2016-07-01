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

import algorithms.forculus.forculus as forculus
import utils.file_util as file_util

class HelpQueryRandomizer:
  """ A Randomizer that extracts the help query string from an |Entry| and uses
  Forculus threshold encryption to emit an encrypted version of it.
  """

  def randomize(self, entries):
    """ Extracts the help query string from each |Entry| in |entries| and
    uses Forculus threshold encryption to emit an encrypted version of it.

    This function does not return anything but it writes a file
    into the "r_to_s" output directory containing the randomizer output.

    Args:
      entries {list of Entry}: The entries to be randomized.
    """
    with file_util.openFileForReading(
        file_util.FORCULUS_HELP_QUERY_CONFIG, file_util.CONFIG_DIR) as cf:
      config = forculus.Config.from_csv(cf)

    with file_util.openForRandomizerWriting(
        file_util.HELP_QUERY_RANDOMIZER_OUTPUT_FILE_NAME) as f:
      forculus_inserter = forculus.ForculusInserter(config.threshold, f)
      for entry in entries:
        forculus_inserter.Insert(entry.help_query)
