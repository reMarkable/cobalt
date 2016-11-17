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

_logger = logging.getLogger()

def run_all_tests(dirs):
  tdir = os.path.abspath(os.path.join(SRC_ROOT_DIR, 'out', *dirs))

  if not os.path.exists(tdir):
    print "\n*************** ERROR ****************"
    print "Directory %s does not exist." % tdir
    print "Run 'cobaltb.py build' first."
    return
  print "Running all tests in %s " % tdir
  for test_executable in os.listdir(tdir):
    print "Running %s..." % test_executable
    path = os.path.abspath(os.path.join(tdir, test_executable))
    subprocess.check_call([path, '--logtostderr=1'], shell=True)
    print "DONE!"


