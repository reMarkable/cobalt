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
import json
import logging
import os
import shutil
import subprocess
import sys

import tools.container_util as container_util
import tools.cpplint as cpplint
import tools.golint as golint
import tools.process_starter as process_starter
import tools.test_runner as test_runner

from tools.test_runner import E2E_TEST_ANALYZER_PUBLIC_KEY_PEM

from tools.process_starter import DEFAULT_SHUFFLER_PORT
from tools.process_starter import DEFAULT_ANALYZER_SERVICE_PORT
from tools.process_starter import DEFAULT_REPORT_MASTER_PORT
from tools.process_starter import DEMO_CONFIG_DIR
from tools.process_starter import SHUFFLER_DEMO_CONFIG_FILE

THIS_DIR = os.path.abspath(os.path.dirname(__file__))
OUT_DIR = os.path.abspath(os.path.join(THIS_DIR, 'out'))
SYSROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, 'sysroot'))
SERVICE_ACCOUNT_CREDENTIALS_FILE = os.path.join(THIS_DIR,
    'service_account_credentials.json')
PERSONAL_CLUSTER_JSON_FILE = os.path.join(THIS_DIR, 'personal_cluster.json')

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

def _setup(args):
  subprocess.check_call(["git", "submodule", "init"])
  subprocess.check_call(["git", "submodule", "update"])
  subprocess.check_call(["./setup.sh", "-d"])

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

# Specifiers of subsets of tests to run
TEST_FILTERS =['all', 'gtests', 'nogtests', 'gotests', 'nogotests',
               'btemulator', 'nobtemulator', 'e2e', 'noe2e', 'cloud_bt', 'perf']

# Returns 0 if all tests return 0, otherwise returns 1.
def _test(args):
  # A map from positive filter specifiers to the list of test directories
  # it represents. Note that 'cloud_bt' and 'perf' tests are special. They are
  # not included in 'all'. They are only run if asked for explicitly.
  FILTER_MAP = {
    'all': ['gtests', 'go_tests', 'gtests_btemulator', 'e2e_tests'],
    'gtests': ['gtests'],
    'gotests' : ['go_tests'],
    'btemulator': ['gtests_btemulator'],
    'e2e': ['e2e_tests'],
    'cloud_bt' : ['gtests_cloud_bt'],
    'perf' : ['perf_tests']
  }

  # A list of test directories for which the contained tests assume the
  # existence of a running instance of the Bigtable Emulator.
  NEEDS_BT_EMULATOR=['gtests_btemulator', 'e2e_tests']

  # A list of test directories for which the contained tests assume the
  # existence of a running instance of the Cobalt processes (Shuffler,
  # Analyzer Service, Report Master.)
  NEEDS_COBALT_PROCESSES=['e2e_tests']

  # Get the list of test directories we should run.
  if args.tests.startswith('no'):
    test_dirs = [test_dir for test_dir in FILTER_MAP['all']
        if test_dir not in FILTER_MAP[args.tests[2:]]]
  else:
    test_dirs = FILTER_MAP[args.tests]

  success = True
  print ("Will run tests in the following directories: %s." %
      ", ".join(test_dirs))

  bigtable_project_name = ''
  bigtable_instance_name = ''
  for test_dir in test_dirs:
    start_bt_emulator = ((test_dir in NEEDS_BT_EMULATOR)
        and not args.use_cloud_bt)
    start_cobalt_processes = (test_dir in NEEDS_COBALT_PROCESSES)
    test_args = None
    if (test_dir == 'gtests_cloud_bt'):
      if not os.path.exists(SERVICE_ACCOUNT_CREDENTIALS_FILE):
        print ('You must first create the file %s.' %
               SERVICE_ACCOUNT_CREDENTIALS_FILE)
        print 'See the instructions in README.md.'
        return
      if args.bigtable_project_name == '':
        print '--bigtable_project_name must be specified'
        success = False
        break
      if args.bigtable_instance_name == '':
        print '--bigtable_instance_name must be specified'
        success = False
        break
      test_args = [
          "--bigtable_project_name=%s" % args.bigtable_project_name,
          "--bigtable_instance_name=%s" % args.bigtable_instance_name
      ]
      bigtable_project_name = args.bigtable_project_name
      bigtable_instance_name = args.bigtable_instance_name
    if (test_dir == 'e2e_tests'):
      test_args = [
          "-analyzer_uri=localhost:%d" % DEFAULT_ANALYZER_SERVICE_PORT,
          "-analyzer_pk_pem_file=%s" % E2E_TEST_ANALYZER_PUBLIC_KEY_PEM,
          "-shuffler_uri=localhost:%d" % DEFAULT_SHUFFLER_PORT,
          "-report_master_uri=localhost:%d" % DEFAULT_REPORT_MASTER_PORT,
          ("-observation_querier_path=%s" %
              process_starter.OBSERVATION_QUERIER_PATH),
          "-test_app_path=%s" % process_starter.TEST_APP_PATH,
          "-sub_process_v=%d"%_verbose_count
      ]
      if args.use_cloud_bt:
        test_args = test_args + [
          "-bigtable_project_name=%s" % args.bigtable_project_name,
          "-bigtable_instance_name=%s" % args.bigtable_instance_name,
        ]
        bigtable_project_name = args.bigtable_project_name
        bigtable_instance_name = args.bigtable_instance_name
    print '********************************************************'
    success = (test_runner.run_all_tests(
        test_dir, start_bt_emulator=start_bt_emulator,
        start_cobalt_processes=start_cobalt_processes,
        bigtable_project_name=bigtable_project_name,
        bigtable_instance_name=bigtable_instance_name,
        verbose_count=_verbose_count,
        test_args=test_args) == 0) and success

  print
  if success:
    print '******************* ALL TESTS PASSED *******************'
    return 0
  else:
    print '******************* SOME TESTS FAILED *******************'
    return 1

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

def _start_bigtable_emulator(args):
  process_starter.start_bigtable_emulator()


def _start_shuffler(args):
  process_starter.start_shuffler(port=args.port,
                                 analyzer_uri=args.analyzer_uri,
                                 use_memstore=args.use_memstore,
                                 erase_db=(not args.keep_existing_db),
                                 config_file=args.config_file,
                                 # Because it makes the demo more interesting
                                 # we use verbose_count at least 3.
                                 verbose_count=max(3, _verbose_count))

def _start_analyzer_service(args):
  process_starter.start_analyzer_service(port=args.port,
      # Because it makes the demo more interesting
      # we use verbose_count at least 3.
      verbose_count=max(3, _verbose_count))

def _start_report_master(args):
  process_starter.start_report_master(port=args.port,
                                      cobalt_config_dir=args.cobalt_config_dir,
                                      verbose_count=_verbose_count)

def _start_test_app(args):
  process_starter.start_test_app(shuffler_uri=args.shuffler_uri,
                                 analyzer_uri=args.analyzer_uri,
                                 # Because it makes the demo more interesting
                                 # we use verbose_count at least 3.
                                 verbose_count=max(3, _verbose_count))

def _start_report_client(args):
  process_starter.start_report_client(
      report_master_uri=args.report_master_uri,
      verbose_count=_verbose_count)

def _start_observation_querier(args):
  bigtable_project_name = ''
  bigtable_instance_name = ''
  if args.use_cloud_bt:
    bigtable_project_name = args.bigtable_project_name
    bigtable_instance_name = args.bigtable_instance_name
  process_starter.start_observation_querier(
      bigtable_project_name=bigtable_project_name,
      bigtable_instance_name=bigtable_instance_name,
      verbose_count=_verbose_count)

def _generate_keys(args):
  path = os.path.join(OUT_DIR, 'tools', 'key_generator', 'key_generator')
  subprocess.check_call([path])

def _provision_bigtable(args):
  if not os.path.exists(SERVICE_ACCOUNT_CREDENTIALS_FILE):
    print ('You must first create the file %s.' %
           SERVICE_ACCOUNT_CREDENTIALS_FILE)
    print 'See the instructions in README.md.'
    return
  if args.bigtable_project_name == '':
    print '--bigtable_project_name must be specified'
    return
  if args.bigtable_instance_name == '':
    print '--bigtable_instance_name must be specified'
    return

  path = os.path.join(OUT_DIR, 'tools', 'bigtable_tool', 'bigtable_tool')
  subprocess.check_call([path,
      "--bigtable_project_name", args.bigtable_project_name,
      "--bigtable_instance_name", args.bigtable_instance_name])

def _deploy_show(args):
  container_util.display()

def _deploy_authenticate(args):
  container_util.authenticate(args.cluster_name, args.cloud_project_prefix,
      args.cloud_project_name)

def _deploy_build(args):
  container_util.build_all_docker_images(
      shuffler_config_file=args.shuffler_config_file)

def _deploy_push(args):
  if args.job == 'shuffler':
    container_util.push_shuffler_to_container_registry(
        args.cloud_project_prefix, args.cloud_project_name)
  elif args.job == 'analyzer-service':
    container_util.push_analyzer_service_to_container_registry(
        args.cloud_project_prefix, args.cloud_project_name)
  elif args.job == 'report-master':
    container_util.push_report_master_to_container_registry(
        args.cloud_project_prefix, args.cloud_project_name)
  else:
    print('Unknown job "%s". I only know how to push "shuffler", '
          '"analyzer-service" and "report-master".' % args.job)

def _deploy_start(args):
  if args.job == 'shuffler':
    container_util.start_shuffler(args.cloud_project_prefix,
                                  args.cloud_project_name,
                                  args.gce_pd_name)
  elif args.job == 'analyzer-service':
    if args.bigtable_instance_name == '':
        print '--bigtable_instance_name must be specified'
        return
    container_util.start_analyzer_service(
        args.cloud_project_prefix, args.cloud_project_name,
        args.bigtable_instance_name)
  elif args.job == 'report-master':
    if args.bigtable_instance_name == '':
        print '--bigtable_instance_name must be specified'
        return
    container_util.start_report_master(
        args.cloud_project_prefix, args.cloud_project_name,
        args.bigtable_instance_name)
  else:
    print('Unknown job "%s". I only know how to start "shuffler", '
          '"analyzer-service" and "report-master".' % args.job)

def _deploy_stop(args):
  if args.job == 'shuffler':
    container_util.stop_shuffler()
  elif args.job == 'analyzer-service':
    container_util.stop_analyzer_service()
  elif args.job == 'report-master':
    container_util.stop_report_master()
  else:
    print('Unknown job "%s". I only know how to stop "shuffler", '
          '"analyzer-service" and "report-master".' % args.job)

def _deploy_upload_secret_key(args):
  container_util.create_analyzer_private_key_secret()

def _deploy_delete_secret_key(args):
  container_util.delete_analyzer_private_key_secret()

def main():
  personal_cluster_settings = {
    'cloud_project_prefix': '',
    'cloud_project_name': '',
    'cluster_name': '',
    'gce_pd_name': '',
    'bigtable_project_name' : '',
    'bigtable_instance_name': '',
  }
  if os.path.exists(PERSONAL_CLUSTER_JSON_FILE):
    print ('Default deployment options will be taken from %s.' %
           PERSONAL_CLUSTER_JSON_FILE)
    with open(PERSONAL_CLUSTER_JSON_FILE) as f:
      personal_cluster_settings = json.load(f)

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

  sub_parser = subparsers.add_parser('setup', parents=[parent_parser],
    help='Sets up the build environment.')
  sub_parser.set_defaults(func=_setup)

  sub_parser = subparsers.add_parser('build', parents=[parent_parser],
    help='Builds Cobalt.')
  sub_parser.set_defaults(func=_build)

  sub_parser = subparsers.add_parser('lint', parents=[parent_parser],
    help='Run language linters on all source files.')
  sub_parser.set_defaults(func=_lint)

  sub_parser = subparsers.add_parser('test', parents=[parent_parser],
    help='Runs Cobalt tests. You must build first.')
  sub_parser.set_defaults(func=_test)
  sub_parser.add_argument('--tests', choices=TEST_FILTERS,
      help='Specify a subset of tests to run. Default=all',
      default='all')
  sub_parser.add_argument('-use_cloud_bt',
      help='Causes the end-to-end tests to run against an instance of Cloud '
      'Bigtable. Otherwise a local instance of the Bigtable Emulator will be '
      'used.', action='store_true')
  sub_parser.add_argument('--bigtable_project_name',
      help='Specify a Cloud project against which to run some of the tests.'
      ' Only used for the cloud_bt tests and e2e tests when -use_cloud_bt is'
      ' specified.'
      ' default=%s'%personal_cluster_settings['bigtable_project_name'],
      default=personal_cluster_settings['bigtable_project_name'])
  sub_parser.add_argument('--bigtable_instance_name',
      help='Specify a Cloud Bigtable instance within the specified Cloud'
      ' project against which to run some of the tests.'
      ' Only used for the cloud_bt tests and e2e tests in cloud mode.'
      ' default=%s'%personal_cluster_settings['bigtable_instance_name'],
      default=personal_cluster_settings['bigtable_instance_name'])

  sub_parser = subparsers.add_parser('clean', parents=[parent_parser],
    help='Deletes some or all of the build products.')
  sub_parser.set_defaults(func=_clean)
  sub_parser.add_argument('--full',
      help='Delete the entire "out" directory.',
      action='store_true')

  ########################################################
  # start command
  ########################################################
  start_parser = subparsers.add_parser('start',
    help='Start one of the Cobalt processes running locally.')
  start_subparsers = start_parser.add_subparsers()

  sub_parser = start_subparsers.add_parser('shuffler',
      parents=[parent_parser], help='Start the Shuffler running locally.')
  sub_parser.set_defaults(func=_start_shuffler)
  sub_parser.add_argument('--port',
      help='The port on which the Shuffler should listen. '
           'Default=%s.' % DEFAULT_SHUFFLER_PORT,
      default=DEFAULT_SHUFFLER_PORT)
  sub_parser.add_argument('--analyzer_uri',
      help='Default=localhost:%s'%DEFAULT_ANALYZER_SERVICE_PORT,
      default='localhost:%s'%DEFAULT_ANALYZER_SERVICE_PORT)
  sub_parser.add_argument('-use_memstore',
      help='Default: False, use persistent LevelDB Store.',
      action='store_true')
  sub_parser.add_argument('-keep_existing_db',
      help='When using LevelDB should any previously persisted data be kept? '
      'Default=False, erase the DB before starting the Shuffler.',
      action='store_true')
  sub_parser.add_argument('--config_file',
      help='Path to the Shuffler configuration file. '
           'Default=%s' % SHUFFLER_DEMO_CONFIG_FILE,
      default=SHUFFLER_DEMO_CONFIG_FILE)

  sub_parser = start_subparsers.add_parser('analyzer_service',
      parents=[parent_parser], help='Start the Analyzer Service running locally'
          ' and connected to a local instance of the Bigtable Emulator.')
  sub_parser.set_defaults(func=_start_analyzer_service)
  sub_parser.add_argument('--port',
      help='The port on which the Analyzer service should listen. '
           'Default=%s.' % DEFAULT_ANALYZER_SERVICE_PORT,
      default=DEFAULT_ANALYZER_SERVICE_PORT,
      )

  sub_parser = start_subparsers.add_parser('report_master',
      parents=[parent_parser], help='Start the Analyzer ReportMaster Service '
          'running locally and connected to a local instance of the Bigtable'
          'Emulator.')
  sub_parser.set_defaults(func=_start_report_master)
  sub_parser.add_argument('--port',
      help='The port on which the ReportMaster should listen. '
           'Default=%s.' % DEFAULT_REPORT_MASTER_PORT,
      default=DEFAULT_REPORT_MASTER_PORT)
  sub_parser.add_argument('--cobalt_config_dir',
      help='Path of directory containing Cobalt configuration files. '
           'Default=%s' % DEMO_CONFIG_DIR,
      default=DEMO_CONFIG_DIR)

  sub_parser = start_subparsers.add_parser('test_app',
      parents=[parent_parser], help='Start the Cobalt test client app.')
  sub_parser.set_defaults(func=_start_test_app)
  sub_parser.add_argument('--shuffler_uri',
      help='Default=localhost:%s' % DEFAULT_SHUFFLER_PORT,
      default='localhost:%s' % DEFAULT_SHUFFLER_PORT)
  sub_parser.add_argument('--analyzer_uri',
      help='Default=localhost:%s'%DEFAULT_ANALYZER_SERVICE_PORT,
      default='localhost:%s'%DEFAULT_ANALYZER_SERVICE_PORT)

  sub_parser = start_subparsers.add_parser('report_client',
      parents=[parent_parser], help='Start the Cobalt report client.')
  sub_parser.set_defaults(func=_start_report_client)
  sub_parser.add_argument('--report_master_uri',
      help='Default=localhost:%s' % DEFAULT_REPORT_MASTER_PORT,
      default='localhost:%s' % DEFAULT_REPORT_MASTER_PORT)

  sub_parser = start_subparsers.add_parser('observation_querier',
      parents=[parent_parser], help='Start the Cobalt ObservationStore '
                                    'querying tool.')
  sub_parser.set_defaults(func=_start_observation_querier)
  sub_parser.add_argument('-use_cloud_bt',
      help='Causes the query to be performed against an instance of Cloud '
      'Bigtable. Otherwise a local instance of the Bigtable Emulator will be '
      'used.', action='store_true')
  sub_parser.add_argument('--bigtable_project_name',
      help='Specify a Cloud project against which to query. '
      'Only used if -use_cloud_bt is set. '
      'default=%s'%personal_cluster_settings['bigtable_project_name'],
      default=personal_cluster_settings['bigtable_project_name'])
  sub_parser.add_argument('--bigtable_instance_name',
      help='Specify a Cloud Bigtable instance within the specified Cloud '
      'project against which to query. Only used if -use_cloud_bt is set. '
      'default=%s'%personal_cluster_settings['bigtable_instance_name'],
      default=personal_cluster_settings['bigtable_instance_name'])

  sub_parser = start_subparsers.add_parser('bigtable_emulator',
    parents=[parent_parser],
    help='Start the Bigtable Emulator running locally.')
  sub_parser.set_defaults(func=_start_bigtable_emulator)

  sub_parser = subparsers.add_parser('keygen', parents=[parent_parser],
    help='Generate new public/private key pairs.')
  sub_parser.set_defaults(func=_generate_keys)

  sub_parser = subparsers.add_parser('provision_bigtable',
    parents=[parent_parser],
    help="Create Cobalt's Cloud BigTables if they don't exit.")
  sub_parser.set_defaults(func=_generate_keys)
  sub_parser.add_argument('--bigtable_project_name',
      help='Specify the Cloud project containing the Bigtable instance '
      ' to be provisioned. '
      'default=%s'%personal_cluster_settings['bigtable_project_name'],
      default=personal_cluster_settings['bigtable_project_name'])
  sub_parser.add_argument('--bigtable_instance_name',
      help='Specify the Cloud Bigtable instance within the specified Cloud'
      ' project that is to be provisioned. '
      'default=%s'%personal_cluster_settings['bigtable_instance_name'],
      default=personal_cluster_settings['bigtable_instance_name'])
  sub_parser.set_defaults(func=_provision_bigtable)

  ########################################################
  # deploy command
  ########################################################
  deploy_parser = subparsers.add_parser('deploy',
    help='Build Docker containers. Push to Container Regitry. Deploy to GKE.')
  deploy_subparsers = deploy_parser.add_subparsers()

  sub_parser = deploy_subparsers.add_parser('show',
      parents=[parent_parser], help='Display information about currently '
      'deployed jobs on GKE, including their public URIs.')
  sub_parser.set_defaults(func=_deploy_show)

  sub_parser = deploy_subparsers.add_parser('authenticate',
      parents=[parent_parser], help='Refresh your authentication token if '
      'necessary. Also associates your local computer with a particular '
      'GKE cluster to which you will be deploying.')
  sub_parser.set_defaults(func=_deploy_authenticate)
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the Cloud project name to which your are '
           'deploying. This is usually an organization domain name if your '
           'Cloud project is associated with one. Pass the empty string for no '
           'prefix. '
           'Default=%s.' %personal_cluster_settings['cloud_project_prefix'],
      default=personal_cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project to which you are '
           'deploying. This is the full project name if --cloud_project_prefix '
           'is empty. Otherwise the full project name is '
           '<cloud_project_prefix>:<cloud_project_name>. '
           'Default=%s' % personal_cluster_settings['cloud_project_name'],
      default=personal_cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--cluster_name',
      help='The GKE "container cluster" within your Cloud project to which you '
           'are deploying. '
           'Default=%s' % personal_cluster_settings['cluster_name'],
      default=personal_cluster_settings['cluster_name'])

  sub_parser = deploy_subparsers.add_parser('build',
      parents=[parent_parser], help='Rebuild all Docker images. '
          'You must have the Docker daemon running.')
  sub_parser.set_defaults(func=_deploy_build)
  sub_parser.add_argument('--shuffler_config_file',
      help='Path to the Shuffler configuration file. '
           'Default=%s' % SHUFFLER_DEMO_CONFIG_FILE,
      default=SHUFFLER_DEMO_CONFIG_FILE)

  sub_parser = deploy_subparsers.add_parser('push',
      parents=[parent_parser], help='Push a Docker image to the Google'
          'Container Registry.')
  sub_parser.set_defaults(func=_deploy_push)
  sub_parser.add_argument('--job',
      help='The job you wish to push. Valid choices are "shuffler", '
           '"analyzer-service", "report-master". Required.')
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the Cloud project name to which your are '
           'deploying. This is usually an organization domain name if your '
           'Cloud project is associated with one. Pass the empty string for no '
           'prefix. '
           'Default=%s.' %personal_cluster_settings['cloud_project_prefix'],
      default=personal_cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project to which you are '
           'deploying. This is the full project name if --cloud_project_prefix '
           'is empty. Otherwise the full project name is '
           '<cloud_project_prefix>:<cloud_project_name>. '
           'Default=%s' % personal_cluster_settings['cloud_project_name'],
      default=personal_cluster_settings['cloud_project_name'])

  sub_parser = deploy_subparsers.add_parser('start',
      parents=[parent_parser], help='Start one of the jobs on GKE.')
  sub_parser.set_defaults(func=_deploy_start)
  sub_parser.add_argument('--job',
      help='The job you wish to start. Valid choices are "shuffler", '
           '"analyzer-service", "report-master". Required.')
  sub_parser.add_argument('--bigtable_instance_name',
      help='Specify a Cloud Bigtable instance within the specified Cloud '
           'project that the Analyzer should connect to. This is required '
           'if and only if you are starting one of the two Analyzer jobs. '
           'Default=%s' % personal_cluster_settings['bigtable_instance_name'],
      default=personal_cluster_settings['bigtable_instance_name'])
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the Cloud project name to which your are '
           'deploying. This is usually an organization domain name if your '
           'Cloud project is associated with one. Pass the empty string for no '
           'prefix. '
           'Default=%s.' %personal_cluster_settings['cloud_project_prefix'],
      default=personal_cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project to which you are '
           'deploying. This is the full project name if --cloud_project_prefix '
           'is empty. Otherwise the full project name is '
           '<cloud_project_prefix>:<cloud_project_name>. '
           'Default=%s' % personal_cluster_settings['cloud_project_name'],
      default=personal_cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--gce_pd_name',
      help='The name of a GCE persistent disk. This is used only when starting '
           'the Shuffler. The disk must already have been created in the same '
           'Cloud project in which the Shuffler is being deployed. '
           'Default=%s' % personal_cluster_settings['gce_pd_name'],
      default=personal_cluster_settings['gce_pd_name'])

  sub_parser = deploy_subparsers.add_parser('stop',
      parents=[parent_parser], help='Stop one of the jobs on GKE.')
  sub_parser.set_defaults(func=_deploy_stop)
  sub_parser.add_argument('--job',
      help='The job you wish to stop. Valid choices are "shuffler", '
           '"analyzer-service", "report-master". Required.')

  sub_parser = deploy_subparsers.add_parser('upload_secret_key',
      parents=[parent_parser], help='Creates a |secret| object in the '
      'cluster to store a private key for the Analyzer. The private key must '
      'first be generated using the "generate_keys" command. This must be done '
      'at least once before starting the Analyzer Service. To replace the '
      'key first delete the old one using the "deploy delete_secret_key" '
      'command.')
  sub_parser.set_defaults(func=_deploy_upload_secret_key)

  sub_parser = deploy_subparsers.add_parser('delete_secret_key',
      parents=[parent_parser], help='Deletes a |secret| object in the '
      'cluster that was created using the "deploy upload_secret_key" '
      'command.')
  sub_parser.set_defaults(func=_deploy_delete_secret_key)


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

  os.environ["GOOGLE_APPLICATION_CREDENTIALS"] = \
      SERVICE_ACCOUNT_CREDENTIALS_FILE

  os.environ["GRPC_DEFAULT_SSL_ROOTS_FILE_PATH"] = os.path.abspath(
      os.path.join(SYSROOT_DIR, 'share', 'grpc', 'roots.pem'))

  os.environ["GOROOT"] = "%s/golang" % SYSROOT_DIR

  return args.func(args)


if __name__ == '__main__':
  sys.exit(main())
