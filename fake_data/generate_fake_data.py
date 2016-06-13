#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

""" This script creates several csv files containing fake data to be visualized.
"""

import collections
import csv
import os
import random

# For the names of the modules in our fake data we will use names from a
# list of "girl's names" found on the internet.
from girls_names import GIRLS_NAMES
# We will use cities from a list of cities found on the internet.
from us_cities import US_CITIES
# Total number of users
NUM_USERS = 1000

THIS_DIR = os.path.dirname(__file__)
OUT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir,
    'out'))

# The name of the file we write containing the synthetic, random input data.
# This will be the input to both the straight counting pipeline and the
# Cobalt prototype pipeline.
GENERATED_INPUT_DATA_FILE_NAME = 'input_data.csv'

# The names of the csv files that constitute the output from the straight
# counting pipeline.
USAGE_BY_MODULE_CSV_FILE_NAME = 'usage_by_module.csv'
USAGE_BY_CITY_CSV_FILE_NAME = 'usage_and_rating_by_city.csv'
USAGE_BY_HOUR_CSV_FILE_NAME = 'usage_by_hour.csv'

# This defines a new type |Entry| as a tuple with five named fields.
Entry = collections.namedtuple('Entry',['user_id', 'name', 'city', 'hour',
  'rating'])

# A pair consisting of a usage and a rating.
class UsageAndRating:
  def  __init__(self, rating):
  	self.num_uses = 1
  	self.total_rating = rating

def normalRandomInt(max_val, spread, skew=0):
  """Returns a random integer from a normal distribution whose parameters may be
  tweaked by setting max_val, spread and skew. The value is clipped to
  the range [0, max_val].

  Args:
    max_val {number} A positive number. All returned values will be less than
      this.
    spread {float} Should be a value between 0 and 1. The standard deviation of
      the normal distribution will be set to this value times max_val.
    skew {float} Should be value between -1 and 1. The mean of the normal
      distribution will be set to max_val*(0.5 + skew).

  Returns:
    {int} A random integer in the range [0, max_val].
  """
  x  = int(random.normalvariate(max_val*(0.5 + skew), max_val*spread))
  # Ensure the value is in the range [0, max_val]
  return max(0, min(x, max_val))

def generateRandomEntries(num_entries):
  """Generates a random list of Entries.

  Args:
    num_entries {int} The number of random entries to generate.
  Returns:
    {list of Entry} A list of random entries of length |num_entries|.
  """
  entries = []
  for i in xrange(num_entries):
    # The spread and skew parameters used below were obtained by
    # experimentation. They yield output files that look somewhat natural.
    city_index = normalRandomInt(len(US_CITIES)-1, 0.035, skew=0.25)
    city = US_CITIES[city_index]
    name_index = normalRandomInt(len(GIRLS_NAMES)-1, 0.001)
    name = GIRLS_NAMES[name_index]
    hour = int(random.triangular(0,23))
    rating = random.randint(0, 10)
    user_id = random.randint(1, NUM_USERS + 1)
    entries.append(Entry(user_id, name, city, hour, rating))
  return entries

def writeEntries(entries, file_name):
  """ Writes a csv file containing the given entries.

  Args:
    entries {list of Entry}: A list of Entries to be written.
    file_name {string}: The csv file to generate.
  """
  with open(os.path.join(OUT_DIR, file_name), 'w+b') as f:
    writer = csv.writer(f)
    for entry in entries:
      writer.writerow([entry.user_id, entry.name, entry.city, entry.hour,
                       entry.rating])


class Accumulator:
  """Accumulates the randomly produced entries and aggregates stats about them.
  """
  def  __init__(self):
    # A map from city name to UsageAnRating
  	self.usage_and_rating_by_city={}
    # A counter used to count occurrences of each module seen.
  	self.usage_by_module=collections.Counter()
    # A list of 24 singleton lists of usage counts.
  	self.usage_by_hour=[[0] for i in xrange(24)]

  def addEntry(self, entry):
  	self.usage_by_module[entry.name] +=1
  	self.usage_by_hour[entry.hour][0] += 1
  	if entry.city in self.usage_and_rating_by_city:
  	  self.usage_and_rating_by_city[entry.city].num_uses = (
          self.usage_and_rating_by_city[entry.city].num_uses + 1)
  	  self.usage_and_rating_by_city[entry.city].total_rating +=entry.rating
  	else:
  	  self.usage_and_rating_by_city[entry.city] = UsageAndRating(entry.rating)

def main():
  # Create the out directory.
  if not os.path.exists(OUT_DIR):
    os.makedirs(OUT_DIR)

  # Generate the synthetic input data.
  entries = generateRandomEntries(10000)

  # Write the synthetic input data to a file for consumption by the
  # Cobalt prototype.
  writeEntries(entries, GENERATED_INPUT_DATA_FILE_NAME)

  # Start the straight counting pipeline. We don't bother reading the input
  # file that we just wrote since we already have it in memory.
  # We just use data that is already in memory in |entries|.
  accumulator = Accumulator()
  for entry in entries:
  	accumulator.addEntry(entry)

  with open(os.path.join(OUT_DIR, USAGE_BY_HOUR_CSV_FILE_NAME), 'w+b') as f:
    writer = csv.writer(f)
    writer.writerows(accumulator.usage_by_hour)

  with open(os.path.join(OUT_DIR, USAGE_BY_MODULE_CSV_FILE_NAME), 'w+b') as f:
    writer = csv.writer(f)
    for name in accumulator.usage_by_module:
      writer.writerow([name, accumulator.usage_by_module[name]])

  with open(os.path.join(OUT_DIR, USAGE_BY_CITY_CSV_FILE_NAME), 'w+b') as f:
    writer = csv.writer(f)
    for city in accumulator.usage_and_rating_by_city:
      num_uses = accumulator.usage_and_rating_by_city[city].num_uses
      avg_rating = (accumulator.usage_and_rating_by_city[city].total_rating /
      	            float(num_uses))
      writer.writerow([city, num_uses, int(avg_rating)])

if __name__ == '__main__':
  main()
