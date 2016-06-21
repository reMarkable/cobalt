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

"""The Cobalt command-line interface."""

import argparse
import logging
import shutil
import sys

import analyzers.analyzer as analyzer
import fake_data.generate_fake_data as fake_data
import randomizers.randomizer as randomizer
import shufflers.shuffler as shuffler
import utils.file_util as file_util
import visualization.generate_data_js as visualization

_logger = logging.getLogger()
_verbose_count = 0

def _initLogging(verbose_count):
  """Ensures that the logger (obtained via logging.getLogger(), as usual) is
  initialized, with the log level set as appropriate for |verbose_count|
  instances of --verbose on the command line."""
  assert(verbose_count >= 0)
  if verbose_count == 0:
    level = logging.WARNING
  elif verbose_count == 1:
    level = logging.INFO
  else:  # verbose_count >= 2
    level = logging.DEBUG
  logging.basicConfig(format="%(relativeCreated).3f:%(levelname)s:%(message)s")
  logger = logging.getLogger()
  logger.setLevel(level)
  logger.debug("Initialized logging: verbose_count=%d, level=%d" %
               (verbose_count, level))

def _test():
  print "Test"

def _run_all():
  print "Deleting the out directory..."
  shutil.rmtree(file_util.OUT_DIR, ignore_errors=True)

  # Generate fake data and run the straight-counting pipeline
  fake_data.main()

  # Run the randomizers
  randomizer.main()

  # Run the shufflers
  shuffler.main()

  # Run the analyzers
  analyzer.main()

  # Generate the visualization
  visualization.main()

  print "Done."

def main():
  parser = argparse.ArgumentParser(description='The Cobalt command-line '
      'interface.')

  parent_parser = argparse.ArgumentParser(add_help=False)

  parent_parser.add_argument('--verbose',
    help='Be verbose (multiple times for more)',
    default=0, dest='verbose_count', action='count')

  subparsers = parser.add_subparsers()

  run_parser = subparsers.add_parser('run', parents=[parent_parser],
    help='Runs the synthetic data generator, the straight '
    'counting pipeline and the Cobalt prototype pipeline.')
  run_parser.set_defaults(func=_run_all)

  test_parser = subparsers.add_parser('test', parents=[parent_parser],
    help='Runs the Cobalt tests.')
  test_parser.set_defaults(func=_test)

  args = parser.parse_args()
  global _verbose_count
  _verbose_count = args.verbose_count
  _initLogging(_verbose_count)

  return args.func()


if __name__ == '__main__':
  sys.exit(main())
