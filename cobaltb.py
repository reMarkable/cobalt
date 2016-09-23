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

"""The Cobalt build system command-line interface."""

import argparse
import logging
import os
import shutil
import subprocess
import sys

import tools.cpplint as cpplint
import tools.golint as golint
import tools.test_runner as test_runner

THIS_DIR = os.path.dirname(__file__)
OUT_DIR = os.path.abspath(os.path.join(THIS_DIR, 'out'))

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

def ensureDir(dir_path):
  """Ensures that the directory at |dir_path| exists. If not it is created.

  Args:
    dir_path{string} The path to a directory. If it does not exist it will be
    created.
  """
  if not os.path.exists(dir_path):
    os.makedirs(dir_path)

def _build():
  ensureDir(OUT_DIR)
  savedir = os.getcwd()
  os.chdir(OUT_DIR)
  subprocess.check_call(['cmake', '-G', 'Ninja','..'])
  subprocess.check_call(['ninja'])
  os.chdir(savedir)

def _lint():
  cpplint.main()
  golint.main()

def _test():
  test_runner.run_all_tests(['gtests'])
  test_runner.run_all_tests(['go_tests'])

def _clean():
  print "Deleting the out directory..."
  shutil.rmtree(OUT_DIR, ignore_errors=True)

def main():
  parser = argparse.ArgumentParser(description='The Cobalt command-line '
      'interface.')

  # Note(rudominer) A note about the handling of optional arguments here.
  # We create |parent_parser| and make it a parent of all of our sub parsers.
  # When we want to add a global optional argument (i.e. one that applies
  # to all sub-commands such as --verbose) we add the optional argument
  # to both |parent_parser| and |parser|. The reason for this is that
  # that appears to be the only way to get the help string  to show up both
  # when the top-level command is invoked and when
  # a sub-command is invoked.
  #
  # In other words when the user types:
  #
  #                python cobaltb.py -h
  #
  # and also when the user types
  #
  #                python cobaltb.py test -h
  #
  # we want to show the help for the --verbose option.
  parent_parser = argparse.ArgumentParser(add_help=False)

  parser.add_argument('--verbose',
    help='Be verbose (multiple times for more)',
    default=0, dest='verbose_count', action='count')
  parent_parser.add_argument('--verbose',
    help='Be verbose (multiple times for more)',
    default=0, dest='verbose_count', action='count')

  subparsers = parser.add_subparsers()

  sub_parser = subparsers.add_parser('build', parents=[parent_parser],
    help='Builds Cobalt.')
  sub_parser.set_defaults(func=_build)

  sub_parser = subparsers.add_parser('lint', parents=[parent_parser],
    help='Run language linters on all source files.')
  sub_parser.set_defaults(func=_lint)

  sub_parser = subparsers.add_parser('test', parents=[parent_parser],
    help='Runs Cobalt tests. You must build first.')
  sub_parser.set_defaults(func=_test)

  sub_parser = subparsers.add_parser('clean', parents=[parent_parser],
    help='Deletes the "out" directory.')
  sub_parser.set_defaults(func=_clean)

  args = parser.parse_args()
  global _verbose_count
  _verbose_count = args.verbose_count
  _initLogging(_verbose_count)

  return args.func()


if __name__ == '__main__':
  sys.exit(main())
