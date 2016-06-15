#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

''' Contains the |Entry| data type and functions for reading and writing it.
An |Entry| represents an element of our synthetic randomly-generated
input data.
'''

import collections
import csv

import file_util

# This defines a new type |Entry| as a tuple with six named fields.
Entry = collections.namedtuple('Entry',['user_id', 'name', 'city', 'hour',
  'rating', 'help_query'])

def writeEntries(entries, file_name):
  """ Writes a csv file containing the given entries.

  Args:
    entries {list of Entry}: A list of Entries to be written.
    file_name {string}: The csv file to generate.
  """
  with file_util.openForWriting(file_name) as f:
    writer = csv.writer(f)
    for entry in entries:
      writer.writerow([entry.user_id, entry.name, entry.city, entry.hour,
                       entry.rating, entry.help_query])


def readEntries(file_name):
  """ Reads a csv file and returns a list of Entries

  Args:
    file_name {string}: The csv file to read.

  Returns: A list of Entries.
  """
  with file_util.openForReading(file_name) as csvfile:
    reader = csv.reader(csvfile)
    return [Entry(row[0], row[1], row[2], row[3], row[4], row[5])
        for row in reader]