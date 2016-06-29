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
    return [Entry(int(row[0]), row[1], row[2], int(row[3]), int(row[4]),
                  row[5])
        for row in reader]
