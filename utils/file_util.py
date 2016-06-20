#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

''' Contains utilities for reading and writing files. Also contains constants
for the names of directories and files that are used by more than one
component.
'''

import csv
import os
import sys

# Add the third_party directory to the Python path so that we can import the
# gviz library.
THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
OUT_DIR = os.path.abspath(os.path.join(ROOT_DIR,'out'))
R_TO_S_DIR = os.path.abspath(os.path.join(OUT_DIR,'r_to_s'))
S_TO_A_DIR = os.path.abspath(os.path.join(OUT_DIR,'s_to_a'))

# The name of the file we write containing the synthetic, random input data.
# This will be the input to both the straight counting pipeline and the
# Cobalt prototype pipeline.
GENERATED_INPUT_DATA_FILE_NAME = 'input_data.csv'

# The names of the shuffler output files
SHUFFLER_OUTPUT_FILE_NAME = "shuffler_out.csv"

# The names of the randomizer output files
HELP_QUERY_RANDOMIZER_OUTPUT_FILE_NAME = 'help_query_randomizer_out.csv'

# The csv files written by the direct-counting pipeline
USAGE_BY_MODULE_CSV_FILE_NAME = 'usage_by_module.csv'
USAGE_BY_CITY_CSV_FILE_NAME = 'usage_and_rating_by_city.csv'
USAGE_BY_HOUR_CSV_FILE_NAME = 'usage_by_hour.csv'
POPULAR_HELP_QUERIES_CSV_FILE_NAME = 'popular_help_queries.csv'

def openForReading(name):
  """Opens the file with the given name for reading. The file is expected to be
  in the |out| directory. Throws an exception if the file does not exist.

  Args:
    name {string} The simple name of the file to be found in the |out| dir.
  """
  file = os.path.join(OUT_DIR, name)
  if not os.path.exists(file):
    raise Exception('File does not exist: %s' % file)
  return open(file, 'rb')

def openForWriting(name):
  # Create the out directory.
  if not os.path.exists(OUT_DIR):
    os.makedirs(OUT_DIR)
  return open(os.path.join(OUT_DIR, name), 'w+b')

def openForRandomizerWriting(name):
  # Create the randomizer out directory.
  if not os.path.exists(R_TO_S_DIR):
    os.makedirs(R_TO_S_DIR)
  return open(os.path.join(R_TO_S_DIR, name), 'w+b')

def openForShufflerWriting(name):
  # Create the shuffler out directory.
  if not os.path.exists(S_TO_A_DIR):
    os.makedirs(S_TO_A_DIR)
  return open(os.path.join(S_TO_A_DIR, name), 'w+b')
