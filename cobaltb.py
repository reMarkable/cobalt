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
SYSROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, 'sysroot'))

IMAGES = ["analyzer", "shuffler"]

GCE_PROJECT = "shuffler-test"
GCE_CLUSTER = "cluster-1"
GCE_TAG = "us.gcr.io/google.com/%s" % GCE_PROJECT

A_BT_INSTANCE = "cobalt-analyzer"
A_BT_TABLE = "observations"
A_BT_TABLE_NAME = "projects/google.com:%s/instances/%s/tables/%s" \
                % (GCE_PROJECT, A_BT_INSTANCE, A_BT_TABLE)

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

def setGCEImages(args):
  """Sets the list of GCE images to be built/deployed/started and stopped.

  Args:
    args{list} List of parsed command line arguments.
  """
  global IMAGES
  if args.shuffler_gce:
    IMAGES = ["shuffler"]
  elif args.analyzer_gce:
    IMAGES = ["analyzer"]

def _build(args):
  ensureDir(OUT_DIR)
  savedir = os.getcwd()
  os.chdir(OUT_DIR)
  subprocess.check_call(['cmake', '-G', 'Ninja','..'])
  subprocess.check_call(['ninja'])
  os.chdir(savedir)

def _lint(args):
  cpplint.main()
  golint.main()

def _test(args):
  test_runner.run_all_tests(['gtests'])
  test_runner.run_all_tests(['go_tests'])

# Files and directories in the out directory to NOT delete when doing
# a partial clean.
TO_SKIP_ON_PARTIAL_CLEAN = [
  'CMakeFiles', 'third_party', '.ninja_deps', '.ninja_log', 'CMakeCache.txt',
  'build.ninja', 'cmake_install.cmake', 'rules.ninja'
]

def _clean(args):
  if args.full:
    print "Deleting the out directory..."
    shutil.rmtree(OUT_DIR, ignore_errors=True)
  else:
    print "Doing a partial clean. Pass --full for a full clean."
    if not os.path.exists(OUT_DIR):
      return
    for f in os.listdir(OUT_DIR):
      if not f in TO_SKIP_ON_PARTIAL_CLEAN:
        full_path = os.path.join(OUT_DIR, f)
        if os.path.isfile(full_path):
          os.remove(full_path)
        else:
           shutil.rmtree(full_path, ignore_errors=True)


def _gce_build(args):
  setGCEImages(args)

  # Copy over the dependencies for the cobalt base image
  cobalt = "%s/cobalt" % OUT_DIR

  if not os.path.exists(cobalt):
    os.mkdir(cobalt)

  for dep in ["lib/libprotobuf.so.10",
              "lib/libgoogleapis.so",
              "lib/libgrpc++.so.1",
              "lib/libgrpc.so.1",
              "lib/libunwind.so.1",
              "share/grpc/roots.pem",
             ]:
    shutil.copy("%s/%s" % (SYSROOT_DIR, dep), cobalt)

  # Copy configuration files
  for conf in ["registered_metrics.txt",
               "registered_encodings.txt",
               "registered_reports.txt"
              ]:
    shutil.copy("%s/config/registered/%s" % (THIS_DIR, conf),
                "%s/analyzer/" % OUT_DIR)

  # Build all images
  for i in ["cobalt"] + IMAGES:
    # copy over the dockerfile
    dstdir = "%s/%s" % (OUT_DIR, i)
    shutil.copy("%s/docker/%s/Dockerfile" % (THIS_DIR, i), dstdir)

    subprocess.check_call(["docker", "build", "-t", i, dstdir])

def _gce_push(args):
  setGCEImages(args)

  for i in IMAGES:
    tag = "%s/%s" % (GCE_TAG, i)
    subprocess.check_call(["docker", "tag", i, tag])
    subprocess.check_call(["gcloud", "docker", "--", "push", tag])

def kube_setup():
  subprocess.check_call(["gcloud", "container", "clusters", "get-credentials",
                         GCE_CLUSTER, "--project",
                         "google.com:%s" % GCE_PROJECT])

def _gce_start(args):
  setGCEImages(args)

  kube_setup()

  for i in IMAGES:
    print("Starting %s" % i)

    if (i == "analyzer"):
      args = ["-table", A_BT_TABLE_NAME,
              "-metrics", "/etc/cobalt/registered_metrics.txt",
              "-reports", "/etc/cobalt/registered_reports.txt",
              "-encodings", "/etc/cobalt/registered_encodings.txt"]

      subprocess.check_call(["kubectl", "run", i, "--image=%s/%s" % (GCE_TAG, i),
                             "--port=8080", "--"] + args)
    else:
      subprocess.check_call(["kubectl", "run", i, "--image=%s/%s" % (GCE_TAG, i),
                             "--port=50051"])

    subprocess.check_call(["kubectl", "expose", "deployment", i,
                           "--type=LoadBalancer"])

def _gce_stop(args):
  setGCEImages(args)

  kube_setup()

  for i in IMAGES:
    print("Stopping %s" % i)

    subprocess.check_call(["kubectl", "delete", "service,deployment", i,])

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
    help='Deletes some or all of the build products.')
  sub_parser.set_defaults(func=_clean)
  sub_parser.add_argument('--full',
      help='Delete the entire "out" directory.',
      action='store_true')

  sub_parser = subparsers.add_parser('gce_build', parents=[parent_parser],
    help='Builds Docker images for GCE.')
  sub_parser.set_defaults(func=_gce_build)
  sub_parser.add_argument('--a',
      help='Builds Analyzer Docker image for GCE.',
      action='store_true', dest='analyzer_gce')
  sub_parser.add_argument('--s',
      help='Builds Shuffler Docker image for GCE.',
      action='store_true', dest='shuffler_gce')

  sub_parser = subparsers.add_parser('gce_push', parents=[parent_parser],
    help='Push docker images to GCE.')
  sub_parser.set_defaults(func=_gce_push)
  sub_parser.add_argument('--a',
      help='Push Analyzer Docker image to GCE.',
      action='store_true', dest='analyzer_gce')
  sub_parser.add_argument('--s',
      help='Push Shuffler Docker image to GCE.',
      action='store_true', dest='shuffler_gce')

  sub_parser = subparsers.add_parser('gce_start', parents=[parent_parser],
    help='Start GCE instances.')
  sub_parser.set_defaults(func=_gce_start)
  sub_parser.add_argument('--a',
      help='Starts Analyzer GCE instance.',
      action='store_true', dest='analyzer_gce')
  sub_parser.add_argument('--s',
      help='Starts Shuffler GCE instance.',
      action='store_true', dest='shuffler_gce')

  sub_parser = subparsers.add_parser('gce_stop', parents=[parent_parser],
    help='Stop GCE instances.')
  sub_parser.set_defaults(func=_gce_stop)
  sub_parser.add_argument('--a',
      help='Stops Analyzer GCE instance.',
      action='store_true', dest='analyzer_gce')
  sub_parser.add_argument('--s',
      help='Stops Shuffler GCE instance.',
      action='store_true', dest='shuffler_gce')

  args = parser.parse_args()
  global _verbose_count
  _verbose_count = args.verbose_count
  _initLogging(_verbose_count)

  # Extend paths to include third-party dependencies
  os.environ["PATH"] = \
      "%s/bin" % SYSROOT_DIR \
      + os.pathsep + "%s/gcloud/google-cloud-sdk/bin" % SYSROOT_DIR \
      + os.pathsep + os.environ["PATH"]

  os.environ["LD_LIBRARY_PATH"] = "%s/lib" % SYSROOT_DIR

  return args.func(args)


if __name__ == '__main__':
  sys.exit(main())
