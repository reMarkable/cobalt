#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors
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

"""A library with functions to start each of the Cobalt processes locally."""

import os
import subprocess

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.join(THIS_DIR, os.pardir)
OUT_DIR = os.path.abspath(os.path.join(SRC_ROOT_DIR, 'out'))
SYS_ROOT_DIR = os.path.join(SRC_ROOT_DIR, 'sysroot')

REGISTERED_CONFIG_DIR = os.path.abspath(os.path.join(SRC_ROOT_DIR, 'config',
    'registered'))
SHUFFLER_CONFIG_DIR = os.path.abspath(os.path.join(SRC_ROOT_DIR, 'shuffler',
    'src', 'config', 'config_v0.txt'))

DEFAULT_SHUFFLER_PORT=5001
DEFAULT_ANALYZER_SERVICE_PORT=6001
DEFAULT_REPORT_MASTER_PORT=7001


def kill_process(process, name):
  """ Kills the given process if it is running and waits for it to terminate.

  Args:
      process {Popen} A representation of the process to be killed.
          May be None.

      name {String} Name of the process for use in a user-facing message.
  """
  if process and process.poll() is None:
    print "Killing %s..." % name
    process.terminate()
    process.kill()
    process.wait()


def execute_command(cmd, wait):
  """ Executes the given command and optionally waits for it to complete.

  command {list of strings} will be passed to Popen().

  wait {bool} If true we will wait for the command to complete and return
      the result code. If false we will return immediately and return an
      instance of Popen.

  Returns:
    An instance of Popen if wait is false or an integer return code if
    wait is true.
  """
  p = subprocess.Popen(cmd)
  if not wait:
    return p
  return_code = p.wait()
  if return_code < 0:
    print
    print "****** WARNING Process terminated by signal %d" % (- return_code)
  return return_code

def start_bigtable_emulator(wait=True):
  # Note(rudominer) We can pass -port=n to cbtemulator to run on a different
  # port.
  print
  print "Starting the Cloud Bigtable Emulator..."
  print
  path = os.path.abspath(os.path.join(SYS_ROOT_DIR, 'gcloud',
      'google-cloud-sdk', 'platform', 'bigtable-emulator', 'cbtemulator'))
  cmd = [path]
  return execute_command(cmd, wait)

def start_shuffler(port=DEFAULT_SHUFFLER_PORT, config_file=SHUFFLER_CONFIG_DIR,
                   wait=True):
  print
  print "Starting the shuffler..."
  print
  path = os.path.abspath(os.path.join(OUT_DIR, 'shuffler', 'shuffler'))
  cmd = [path,
        "-port", str(port),
        "-config_file", config_file,
        "-logtostderr", "-v=3"]
  return execute_command(cmd, wait)

def start_analyzer_service(port=DEFAULT_ANALYZER_SERVICE_PORT, wait=True):
  print
  print "Starting the analyzer service..."
  print
  print "Will connect to a local Bigtable Emulator instance."
  print "Have you already started the Bigtable Emulator?"
  print
  path = os.path.abspath(os.path.join(OUT_DIR, 'analyzer', 'analyzer_service',
      'analyzer_service'))
  cmd = [path,
      "-for_testing_only_use_bigtable_emulator",
      "-port", str(port),
      "-logtostderr", "-v=3"]
  return execute_command(cmd, wait)

def start_report_master(port=DEFAULT_REPORT_MASTER_PORT,
                        cobalt_config_dir=REGISTERED_CONFIG_DIR, wait=True):
  print
  print "Starting the analyzer ReportMaster service..."
  print
  print "Will connect to a local Bigtable Emulator instance."
  print "Have you already started the Bigtable Emulator?"
  print
  path = os.path.abspath(os.path.join(OUT_DIR, 'analyzer', 'report_master',
      'analyzer_report_master'))
  cmd = [path,
      "-for_testing_only_use_bigtable_emulator",
      "-port", str(port),
      "-cobalt_config_dir", cobalt_config_dir,
      "-logtostderr", "-v=3"]
  return execute_command(cmd, wait)

def start_test_app(shuffler_uri='', analyzer_uri='', wait=True):
  path = os.path.abspath(os.path.join(OUT_DIR, 'tools', 'test_app',
                                      'cobalt_test_app'))
  cmd = [path,
      "-shuffler_uri", shuffler_uri,
      "-analyzer_uri", analyzer_uri,
      "-logtostderr", "-v=3"]
  return execute_command(cmd, wait)

def start_report_client(report_master_uri='', wait=True):
  path = os.path.abspath(os.path.join(OUT_DIR, 'tools', 'report_client'))
  cmd = [path,
      "-report_master_uri", report_master_uri,
      "-logtostderr", "-v=3"]
  return execute_command(cmd, wait)

