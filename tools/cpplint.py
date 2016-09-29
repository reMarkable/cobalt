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

"""Runs cpp lint on all of Cobalt's C++ files."""

import os
import shutil
import subprocess
import sys

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))
CPP_LINT = os.path.join(SRC_ROOT_DIR, 'third_party', 'cpplint', 'cpplint.py')

CPP_DIRS = [
    os.path.join(SRC_ROOT_DIR, 'util', 'crypto_util'),
    os.path.join(SRC_ROOT_DIR, 'analyzer'),
]

def main():
  for dir_path in CPP_DIRS:
    print "\nLinting cpp files in %s...\n" % dir_path
    for f in os.listdir(dir_path):
      if f.endswith('.h') or f.endswith('.cc'):
        full_path = os.path.join(dir_path, f)
        subprocess.call([CPP_LINT,  full_path])
        print

if __name__ == '__main__':
  main()
