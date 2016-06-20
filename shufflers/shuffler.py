#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Shuffles the randomized data across users and removes metadata such as
identifiers, and timestamps. This file also contains utilities common to
all shufflers.
"""

import csv
import os
import sys

THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
sys.path.insert(0, ROOT_DIR)

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
  return entries

def shuffleAllData(entries):
  """Runs all of the randomizers on the given list of entries.

  This function does not return anything but it invokes all of the
  randomizers each of which will write a file
  into the "r_to_s" output directory containing the randomizer output.

  Args:
    entries {list of Entry}: The entries to be randomized.

  Returns:
    {list of Entry} A list of shuffled entries after performing normalization
    and shuffling techniques.
  """
  removeMetaData(entries)
  normalize(entries)
  permute(entries)
  return entries

def main():
  # Read randmized entries from r_to_s/<randomized_input_file>.
  entries = data.readEntries(file_util.GENERATED_INPUT_DATA_FILE_NAME)
  shuffled_entries = shuffleAllData(entries)

  # Write shuffled entries to s_to_a output folder under <shuffler_output_file>.
  with file_util.openForShufflerWriting(
      file_util.SHUFFLER_OUTPUT_FILE_NAME) as output_f:
    writer = csv.writer(output_f)
    for entry in shuffled_entries:
      writer.writerow([entry.name, entry.city, entry.hour,                      entry.rating, entry.help_query])

if __name__ == '__main__':
  main()
