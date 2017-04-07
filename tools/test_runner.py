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

"""Runs all executables (tests) in a directory"""

import logging
import os
import shutil
import subprocess
import sys
import time

import tools.process_starter as process_starter

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))
SYS_ROOT_DIR = os.path.join(SRC_ROOT_DIR, 'sysroot')
E2E_DIR = os.path.join(SRC_ROOT_DIR, "end_to_end_tests")
E2E_TEST_ANALYZER_PRIVATE_KEY_PEM = os.path.join(E2E_DIR,
    "analyzer_private_key.pem.e2e_test")
E2E_TEST_ANALYZER_PUBLIC_KEY_PEM = os.path.join(E2E_DIR,
    "analyzer_public_key.pem.e2e_test")

_logger = logging.getLogger()

def run_all_tests(test_dir,
                  start_bt_emulator=False,
                  start_cobalt_processes=False,
                  verbose_count=0,
                  test_args=None):
  """ Runs the tests in the given directory.

  Optionally also starts various processes that may be needed by the tests.

  Args:
      test_dir {string} Name of the directory under the "out" directory
      containing test executables to be run.

      start_bt_emulator{ bool} If True then an instance of the Cloud Bigtable
      Emulator will be started before each test and killed afterwards.

      start_cobalt_processes {bool} If True then an instance of the Cobalt
      Shuffler, Analyzer Service and Report Master will be started before each
      test and killed afterwards.

      args {list of strings} These will be passed to each test executable.

    Returns: 0 if all tests return 0, otherwise returns 1.
  """
  tdir = os.path.abspath(os.path.join(SRC_ROOT_DIR, 'out', test_dir))

  if not os.path.exists(tdir):
    print "\n*************** ERROR ****************"
    print "Directory %s does not exist." % tdir
    print "Run 'cobaltb.py build' first."
    return 1

  if test_args is None:
    test_args = []
  test_args.append('-logtostderr')
  if verbose_count > 0:
    test_args.append('-v=%d'%verbose_count)

  print "Running all tests in %s " % tdir
  print "Test arguments: '%s'" % test_args
  all_passed = True
  for test_executable in os.listdir(tdir):
    bt_emulator_process = None
    shuffler_process = None
    analyzer_service_process = None
    report_master_process = None
    try:
      if start_bt_emulator:
        bt_emulator_process=process_starter.start_bigtable_emulator(wait=False)
      if start_cobalt_processes:
        time.sleep(1)
        analyzer_service_process=process_starter.start_analyzer_service(
            private_key_pem_file=E2E_TEST_ANALYZER_PRIVATE_KEY_PEM,
            verbose_count=verbose_count, wait=False)
        time.sleep(1)
        report_master_process=process_starter.start_report_master(
            verbose_count=verbose_count, wait=False)
        time.sleep(1)
        shuffler_process=process_starter.start_shuffler(wait=False)
      print "Running %s..." % test_executable
      path = os.path.abspath(os.path.join(tdir, test_executable))
      command = [path] + test_args
      return_code = subprocess.call(command)
      all_passed = all_passed and return_code == 0
      if return_code < 0:
        print
        print "****** WARNING Process terminated by signal %d" % (- return_code)
    finally:
      process_starter.kill_process(shuffler_process,
                                   "Shuffler")
      process_starter.kill_process(analyzer_service_process,
                                   "Analyzer Service")
      process_starter.kill_process(report_master_process,
                                   "Report Master")
      process_starter.kill_process(bt_emulator_process,
                                   "Cloud Bigtable Emulator")
  if all_passed:
    return 0
  else:
    return 1


