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

"""Runs all of the shufflers the purpose of which is to shuffle the randomized
data across users and remove metadata such as identifiers, and timestamps.
This file also contains utilities common to all shufflers.
"""

import csv
import os
import random
import sys

THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
sys.path.insert(0, ROOT_DIR)

import city_shuffler
import help_query_shuffler
import module_name_shuffler
import hour_shuffler
import url_shuffler

import utils.data as data
import utils.file_util as file_util

def removeMetaData(entries):
  """Removes any user identifying fields from the randomized input
  such as IP addresses, and userids.

  This method does an in-place removal of pii fields for the entries
  stored in memory.

  Args:
    entries {list of Entry}: The entries to be shuffled.

  Returns:
    {list of Entry} A list of entries after removal of metadata.
  """
  return entries

def normalize(entries):
  """Normalizes the input by stripping any special characters or capital
  letters present in the input data.

  This method does an in-place normalization for the entries stored in
  memory.

  Args:
    entries {list of Entry}: The entries to be shuffled.

  Returns:
    {list of Entry} A list of entries after normalization.
  """
  return entries

def permute(entries):
  """Shuffles the randomized input.

  This method does an in-place permutation for the entries stored in
  memory.

  Args:
    entries {list of Entry}: The entries to be shuffled.

  Returns:
    {list of Entry} A list of entries after permutation.
  """
  random.shuffle(entries)

def shuffleAllData(entries):
  """Performs some common, generic shuffling routines on |entries|.

  Args:
    entries {list of tuples}: The data to be shuffled.

  Returns:
    {list of tuples} The shuffled data.
  """
  # TODO(rudominer) At the moment removeMetaData() and normalize() are
  # empty place-holders. But it is not clear that we can ever make sense
  # of these functions at this generic layer. These may need to be moved
  # down into the individual shufflers that understand the data being
  # shuffled. The permute() method can be implemented here though since
  # we can permute a list of tuples without knowing anything about the
  # tuples.
  removeMetaData(entries)
  normalize(entries)
  permute(entries)
  return entries

def shuffleCSVFiles(input_file, output_file):
  ''' A helper function that my be invoked by individual shufflers.
  It reads randomizer output in the for of a CSV file, performs
  some shuffling, and then writes output to another CSV file to be
  consumed by an analyzer.

  Args:
    input_file {string}: The simple name of the CSV file to be read from
    the 'r_to_s' directory.

    output_file {string}: The simple name of the CSV file to be written in
    the 's_to_a' directory.
  '''
  with file_util.openForShufflerReading(input_file) as f:
    reader = csv.reader(f)
    entries = [entry for entry in reader]
  entries = shuffleAllData(entries)
  with file_util.openForShufflerWriting(output_file) as f:
    writer = csv.writer(f)
    for entry in entries:
      writer.writerow(entry)

def runAllShufflers():
  """Runs all of the shufflers.

  This function does not return anything but it invokes all of the
  shufflers each of which will read a file from the 'r_to_s' directory
  and write a file into the 's_to_a' output directory.
  """
  print "Running the help-query shuffler..."
  hq_shuffler = help_query_shuffler.HelpQueryShuffler()
  hq_shuffler.shuffle()

  print "Running the city shuffler..."
  ct_shuffler = city_shuffler.CityShuffler()
  ct_shuffler.shuffle()

  print "Running the module name shuffler..."
  mn_shuffler = module_name_shuffler.ModuleNameShuffler()
  mn_shuffler.shuffle()

  print "Running the module name shuffler for differentially private release..."
  mn_shuffler.shuffle(for_private_release=True)

  print "Running the hour-of-day shuffler..."
  hr_shuffler = hour_shuffler.HourShuffler()
  hr_shuffler.shuffle()

  print "Running the url shuffler..."
  u_shuffler = url_shuffler.UrlShuffler()
  u_shuffler.shuffle()

def main():
  runAllShufflers()

if __name__ == '__main__':
  main()
