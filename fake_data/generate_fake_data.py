#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

""" This script is the first step in the Cobalt prototype. It
generates synthetic data, writes this data to a file called
input_data.csv, then runs the straight counting pipeline on the
data which emits several csv files to the out directory.
"""

import collections
import csv
import os
import random
import sys

THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
sys.path.insert(0, ROOT_DIR)

import utils.file_util as file_util
import utils.data as data

# For the names of the modules in our fake data we will use names from a
# list of "girl's names" found on the internet.
from girls_names import GIRLS_NAMES
# We will use cities from a list of cities found on the internet.
from us_cities import US_CITIES
# We will use proper nouns from a sample list constructed with few special
# characters such as capital letters, single quotes and a period.
from help_query_primary_nouns import HELP_QUERY_PRIMARY_NOUNS
# We will use verbs from a predefined list of commonly used verbs.
from help_query_verbs import HELP_QUERY_VERBS
# We will use predefined set of secondary nouns commonly used in speaking.
from help_query_secondary_nouns import HELP_QUERY_SECONDARY_NOUNS

# Total number of users
NUM_USERS = 1000

# A pair consisting of a usage and a rating.
class UsageAndRating:
  def  __init__(self, rating):
    self.num_uses = 1
    self.total_rating = rating

def powerRandomInt(max_val):
  """Returns a random integer from the interval [0, max_val],
  using a power-law distribution.

  The underlying probability distribution is given by:
  P(X >= n) = (c/(n+c))^4, for n>=0 an integer, and where we use c=20.

  But if X > max_val is generated then max_val is returned.

  Assuming max_val is sufficiently large the distribution should look
  approximately like the following. We display all values of n for
  which P(n) >= 1%

  P(0)  = 0.177
  P(1)  = 0.139
  P(2)  = 0.111
  P(3)  = 0.089
  P(4)  = 0.072
  P(5)  = 0.059
  P(6)  = 0.049
  P(7)  = 0.040
  P(8)  = 0.034
  P(9)  = 0.028
  P(10) = 0.024
  P(11) = 0.020
  P(12) = 0.017
  P(13) = 0.015
  P(14) = 0.013
  P(15) = 0.011
  P(16) = 0.009

  The mean is approximately 6 and the variance is approximaley 4.4.

  Args:
    max_val {number} A positive number. All returned values will be less than
      this.

  Returns:
    {int} A random integer in the range [0, max_val].
  """
  x  = int(20*random.paretovariate(4) - 20)
  # Ensure the value is in the range [0, max_val]
  return max(0, min(x, max_val))

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

def generateRandomHelpQuery():
  """Generates a random help query string of the form <noun verb noun> from a
  predefined set of words for noun and verb category.

  Returns:
    {string} A random help query string containing three words separated with a
    space.
  """

  help_query = ""
  index = powerRandomInt(len(HELP_QUERY_PRIMARY_NOUNS)-1)
  help_query += HELP_QUERY_PRIMARY_NOUNS[index] + " "
  index = random.randint(0, len(HELP_QUERY_VERBS) - 1)
  help_query += HELP_QUERY_VERBS[index] + " "
  index = random.randint(0, len(HELP_QUERY_SECONDARY_NOUNS) - 1)
  help_query += HELP_QUERY_SECONDARY_NOUNS[index]
  return help_query

def generateRandomEntries(num_entries):
  """Generates a random list of Entries.

  Args:
    num_entries {int} The number of random entries to generate.
  Returns:
    {list of Entry} A list of random entries of length |num_entries|.
  """
  entries = []
  for i in xrange(num_entries):
    city_index = powerRandomInt(len(US_CITIES)-1)
    city = US_CITIES[city_index]
    name_index = powerRandomInt(len(GIRLS_NAMES)-1)
    name = GIRLS_NAMES[name_index]
    hour = int(random.triangular(0,23))
    rating = random.randint(0, 10)
    user_id = random.randint(1, NUM_USERS + 1)
    # Generate free-form help queries from a list of primary nouns, verbs and
    # secondary nouns.
    help_query = generateRandomHelpQuery()
    entries.append(data.Entry(user_id, name, city, hour, rating, help_query))
  return entries

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
    # A counter used to count occurences of each help query.
    self.popular_help_query=collections.Counter()

  def addEntry(self, entry):
    self.usage_by_module[entry.name] +=1
    self.usage_by_hour[entry.hour][0] += 1
    if entry.city in self.usage_and_rating_by_city:
      self.usage_and_rating_by_city[entry.city].num_uses = (
        self.usage_and_rating_by_city[entry.city].num_uses + 1)
      self.usage_and_rating_by_city[entry.city].total_rating +=entry.rating
    else:
      self.usage_and_rating_by_city[entry.city] = UsageAndRating(entry.rating)
      self.popular_help_query[entry.help_query] +=1

def main():
  # Generate the synthetic input data.
  entries = generateRandomEntries(10000)

  # Write the synthetic input data to a file for consumption by the
  # Cobalt prototype.
  data.writeEntries(entries, file_util.GENERATED_INPUT_DATA_FILE_NAME)

  # Start the straight counting pipeline. We don't bother reading the input
  # file that we just wrote since we already have it in memory.
  # We just use data that is already in memory in |entries|.
  accumulator = Accumulator()
  for entry in entries:
  	accumulator.addEntry(entry)

  with file_util.openForWriting(file_util.USAGE_BY_HOUR_CSV_FILE_NAME) as f:
    writer = csv.writer(f)
    writer.writerows(accumulator.usage_by_hour)

  with file_util.openForWriting(file_util.USAGE_BY_MODULE_CSV_FILE_NAME) as f:
    writer = csv.writer(f)
    for name in accumulator.usage_by_module:
      writer.writerow([name, accumulator.usage_by_module[name]])

  with file_util.openForWriting(
      file_util.POPULAR_HELP_QUERIES_CSV_FILE_NAME) as f:
    writer = csv.writer(f)
    writer.writerows(accumulator.popular_help_query.most_common(10))

  with file_util.openForWriting(file_util.USAGE_BY_CITY_CSV_FILE_NAME) as f:
    writer = csv.writer(f)
    for city in accumulator.usage_and_rating_by_city:
      num_uses = accumulator.usage_and_rating_by_city[city].num_uses
      avg_rating = (accumulator.usage_and_rating_by_city[city].total_rating /
      	            float(num_uses))
      writer.writerow([city, num_uses, int(avg_rating)])

if __name__ == '__main__':
  main()
