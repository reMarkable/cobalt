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


import shuffler
import utils.file_util as file_util

class CityShuffler:
  """ A shuffler for the city pipeline
  """

  def shuffle(self):
    ''' This function invokes the generic function shuffleCSVFiles() on
    the city-specific CSV files.
    '''
    shuffler.shuffleCSVFiles(file_util.CITY_RANDOMIZER_OUTPUT_FILE_NAME,
        file_util.CITY_SHUFFLER_OUTPUT_FILE_NAME)
