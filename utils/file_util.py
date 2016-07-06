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

''' Contains utilities for reading and writing files. Also contains constants
for the names of directories and files that are used by more than one
component.
'''

import csv
import os
import sys

# directories
THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
OUT_DIR = os.path.abspath(os.path.join(ROOT_DIR,'out'))

ANALYZER_TMP_OUT_DIR = os.path.abspath(os.path.join(OUT_DIR,'analyzer_tmp'))
CACHE_DIR = os.path.abspath(os.path.join(ROOT_DIR,'cache'))
CONFIG_DIR = os.path.abspath(os.path.join(ROOT_DIR, 'config_files'))
R_TO_S_DIR = os.path.abspath(os.path.join(OUT_DIR,'r_to_s'))
S_TO_A_DIR = os.path.abspath(os.path.join(OUT_DIR,'s_to_a'))
VISUALIZATION_DIR = os.path.abspath(os.path.join(ROOT_DIR, 'visualization'))

# The name of the file we write containing the synthetic, random input data.
# This will be the input to both the straight counting pipeline and the
# Cobalt prototype pipeline.
GENERATED_INPUT_DATA_FILE_NAME = 'input_data.csv'

# The names of config files for RAPPOR
# Each config file should comprise two lines
# Line 1: k,h,m,p,q,f
# Line 2: RAPPOR params (# bloom bits, # hashes, # cohorts, and noise
# probabilities p, q, and f.)
RAPPOR_CITY_NAME_CONFIG = 'rappor_city_name_config.csv'
RAPPOR_RATING_CONFIG = 'rappor_rating_config.csv'
RAPPOR_MODULE_NAME_CONFIG = 'rappor_module_name_config.csv'
RAPPOR_HOUR_CONFIG = 'rappor_hour_config.csv'

# Forculus config files
FORCULUS_HELP_QUERY_CONFIG = 'forculus_help_query_config.csv'

# The names of the randomizer output files
HELP_QUERY_RANDOMIZER_OUTPUT_FILE_NAME = 'help_query_randomizer_out.csv'
CITY_RANDOMIZER_OUTPUT_FILE_NAME = 'city_randomizer_out.csv'
MODULE_NAME_RANDOMIZER_OUTPUT_FILE_NAME = 'module_name_randomizer_out.csv'
HOUR_RANDOMIZER_OUTPUT_FILE_NAME = 'hour_randomizer_out.csv'

# The names of the shuffler output files
HELP_QUERY_SHUFFLER_OUTPUT_FILE_NAME = "help_query_shuffler_out.csv"
CITY_SHUFFLER_OUTPUT_FILE_NAME = "city_shuffler_out.csv"
MODULE_NAME_SHUFFLER_OUTPUT_FILE_NAME = "module_name_shuffler_out.csv"
HOUR_SHUFFLER_OUTPUT_FILE_NAME = "hour_shuffler_out.csv"

# The names of the analyzer output files
HELP_QUERY_ANALYZER_OUTPUT_FILE_NAME = "help_query_analyzer_out.csv"
CITY_ANALYZER_OUTPUT_FILE_NAME = "city_analyzer_out.csv"

# The names of RAPPOR intermediate files
CITY_NAME_COUNTS_FILE_NAME = "city_name_counts.csv"
CITY_CANDIDATES_FILE_NAME = "city_candidates.csv"
CITY_MAP_FILE_NAME = "city_map.csv"

# The csv files written by the direct-counting pipeline
USAGE_BY_MODULE_CSV_FILE_NAME = 'usage_by_module.csv'
USAGE_BY_CITY_CSV_FILE_NAME = 'usage_and_rating_by_city.csv'
USAGE_BY_HOUR_CSV_FILE_NAME = 'usage_by_hour.csv'
POPULAR_HELP_QUERIES_CSV_FILE_NAME = 'popular_help_queries.csv'

# visualization
VISUALIZATION_FILE = os.path.join(VISUALIZATION_DIR, 'visualization.html')

def openFileForReading(file_name, dir_path):
  """Opens the file with the given name in the given directory for reading.
  Throws an exception if the file does not exist.

  Args:
    file_name {string} The simple name of the file.
    dir_path {string} The path of the directory.
  """
  file = os.path.join(dir_path, file_name)
  if not os.path.exists(file):
    raise Exception('File does not exist: %s' % file)
  return open(file, 'rb')

def openForReading(name):
  """Opens the file with the given name for reading. The file is expected to be
  in the |out| directory. Throws an exception if the file does not exist.

  Args:
    name {string} The simple name of the file to be found in the |out| dir.
  """
  return openFileForReading(name, OUT_DIR)

def openForShufflerReading(name):
  """Opens the file with the given name for reading. The file is expected to be
  in the out/r_to_s directory. Throws an exception if the file does not exist.

  Args:
    name {string} The simple name of the file to be found in the |out| dir.
  """
  return openFileForReading(name, R_TO_S_DIR)

def openForAnalyzerReading(name):
  """Opens the file with the given name for reading. The file is expected to be
  in the out/s_to_a directory. Throws an exception if the file does not exist.

  Args:
    name {string} The simple name of the file to be found in the |out| dir.
  """
  return openFileForReading(name, S_TO_A_DIR)

def openFileForWriting(file_name, dir_path):
  # Create the directory if it does not exist.
  if not os.path.exists(dir_path):
    os.makedirs(dir_path)
  return open(os.path.join(dir_path, file_name), 'w+b')

def openForWriting(name):
  return openFileForWriting(name, OUT_DIR)

def openForAnalyzerTempWriting(name):
  return openFileForWriting(name, ANALYZER_TMP_OUT_DIR)

def openForRandomizerWriting(name):
  return openFileForWriting(name, R_TO_S_DIR)

def openForShufflerWriting(name):
  return openFileForWriting(name, S_TO_A_DIR)
