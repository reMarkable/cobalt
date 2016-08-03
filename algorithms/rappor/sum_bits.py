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

"""
Read the RAPPOR'd values on stdin, and sum the bits to produce a Counting Bloom
filter by cohort.  This can then be analyzed by R.

This file contains a stand alone function sumBits as well as command line
argument. It can be called as ./rappor_sum_bits <params file>
"""

import csv
import sys

import third_party.rappor.client.python.rappor as rappor

def sumBits(params, stdin, stdout, additional_decryption_func = None,
            fields = [0, 1], header = False):
  """Sums bits from stdin to stdout with params; fields indicates which
  correspond to (RAPPOR cohort, RAPPOR IRR).

  Args:
    params {list of int}: List of param values from RAPPOR config file.

    stdin {file handle}: A file handle to the input file containing
    randomized data.

    stdout {file handle}: A file handle to the output file for storing
    intermediate results from aggregation based on cohorts.

    additional_decryption_func {function}: If this is not None then the
    decryption function will be applied to each row of data just after reading
    it from |stdin|. The function should accept a tuple of strings representing
    the ciphertext and return a single string representing the plain text.
    The decryption function should be the inverse of the encryption function
    applied in randomizeUsingRappor function.

    fields {list of int}: A list of two integer values to specify RAPPOR cohort
    and IRR for each param.

    header {bool}: If True, analysis computation takes place by omitting the
    header row from the input file.
  """
  if len(fields) != 2:
    raise RuntimeError('Error with length of fields in sumBits')

  csv_in = csv.reader(stdin)
  csv_out = csv.writer(stdout)

  num_cohorts = params.num_cohorts
  num_bloombits = params.num_bloombits

  sums = [[0] * num_bloombits for _ in xrange(num_cohorts)]
  num_reports = [0] * num_cohorts

  for i, row in enumerate(csv_in):
    if additional_decryption_func is not None:
      # The tuple read from csv_in represents a cipher text. Pass the elements
      # of that tuple as arguments to the decryption function receiving
      # back the plaintext which is a single string that is a comma-separated
      # list of fields. Split that string into fields and use that as
      # the value of the read row.
      row = additional_decryption_func(*row).split(",")

    subset_of_row = [row[i] for i in fields]
    try:
      (cohort, irr) = subset_of_row
    except ValueError:
      raise RuntimeError('Error parsing row %r or subset %r' % (row,
                                                                subset_of_row))

    if i == 0 and header == True:
      continue  # skip header

    cohort = int(cohort)
    num_reports[cohort] += 1

    if not len(irr) == params.num_bloombits:
      raise RuntimeError(
          "Expected %d bits, got %r" % (params.num_bloombits, len(irr)))
    for i, c in enumerate(irr):
      bit_num = num_bloombits - i - 1  # e.g. char 0 = bit 15, char 15 = bit 0
      if c == '1':
        sums[cohort][bit_num] += 1
      else:
        if c != '0':
          raise RuntimeError('Invalid IRR -- digits should be 0 or 1')

  for cohort in xrange(num_cohorts):
    # First column is the total number of reports in the cohort.
    row = [num_reports[cohort]] + sums[cohort]
    csv_out.writerow(row)


def main(argv):
  try:
    filename = argv[1]
  except IndexError:
    raise RuntimeError('Usage: ./rappor_sum_bits.py <params file>')
  with open(filename) as f:
    try:
      params = rappor.Params.from_csv(f)
    except rappor.Error as e:
      raise RuntimeError(e)

  SumBits(params, sys.stdin, sys.stdout)


if __name__ == '__main__':
  try:
    main(sys.argv)
  except RuntimeError, e:
    print >>sys.stderr, e.args[0]
    sys.exit(1)
