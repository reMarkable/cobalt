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

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.join(THIS_DIR, os.pardir)
SYS_ROOT_DIR = os.path.join(SRC_ROOT_DIR, 'sysroot')

_logger = logging.getLogger()

def start_bigtable_emulator():
  # Note(rudominer) We can pass -port=n to cbtemulator to run on a different
  # port.
  print "Starting the Cloud Bigtable Emulator on port 9000..."
  path = os.path.abspath(os.path.join(SYS_ROOT_DIR, 'gcloud',
      'google-cloud-sdk', 'platform', 'bigtable-emulator', 'cbtemulator'))
  return subprocess.Popen([path])

# Returns 0 if all tests return 0, otherwise returns 1.
def run_all_tests(test_dir, bigtable_emulator=False):
  tdir = os.path.abspath(os.path.join(SRC_ROOT_DIR, 'out', test_dir))

  if not os.path.exists(tdir):
    print "\n*************** ERROR ****************"
    print "Directory %s does not exist." % tdir
    print "Run 'cobaltb.py build' first."
    return 1
  print "Running all tests in %s " % tdir
  all_passed = True
  for test_executable in os.listdir(tdir):
    bt_emulator_process = None
    try:
      if bigtable_emulator:
        bt_emulator_process = start_bigtable_emulator()
      print "Running %s..." % test_executable
      path = os.path.abspath(os.path.join(tdir, test_executable))
      return_code = subprocess.call([path, '--logtostderr=1'], shell=True)
      all_passed = all_passed and return_code == 0
      if return_code < 0:
        print
        print "****** WARNING Process terminated by signal %d" % (- return_code)
    finally:
      if bt_emulator_process is not None and bt_emulator_process.poll() is None:
        print "Killing Cloud Bigtable Emulator"
        bt_emulator_process.terminate()
        bt_emulator_process.kill()
  if all_passed:
    return 0
  else:
    return 1


