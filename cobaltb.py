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
from tools.test_runner import E2E_TEST_SHUFFLER_PUBLIC_KEY_PEM

from tools.process_starter import DEFAULT_ANALYZER_PRIVATE_KEY_PEM
from tools.process_starter import DEFAULT_ANALYZER_PUBLIC_KEY_PEM
from tools.process_starter import DEFAULT_SHUFFLER_PRIVATE_KEY_PEM
from tools.process_starter import DEFAULT_SHUFFLER_PUBLIC_KEY_PEM
from tools.process_starter import DEFAULT_ANALYZER_SERVICE_PORT
from tools.process_starter import DEFAULT_SHUFFLER_PORT
from tools.process_starter import DEFAULT_REPORT_MASTER_PORT
from tools.process_starter import DEMO_CONFIG_DIR
from tools.process_starter import SHUFFLER_DEMO_CONFIG_FILE

THIS_DIR = os.path.abspath(os.path.dirname(__file__))
OUT_DIR = os.path.abspath(os.path.join(THIS_DIR, 'out'))
SYSROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, 'sysroot'))
PERSONAL_BT_ADMIN_SERVICE_ACCOUNT_CREDENTIALS_FILE = os.path.join(THIS_DIR,
    'personal_bt_admin_service_account.json')
PERSONAL_CLUSTER_JSON_FILE = os.path.join(THIS_DIR, 'personal_cluster.json')
BIGTABLE_TOOL_PATH = \
    os.path.join(OUT_DIR, 'tools', 'bigtable_tool', 'bigtable_tool')

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

def _compound_project_name(args):
  """ Builds a compound project name such as google.com:my-project

  Args:
    args: A namespace object as returned from the parse_args() function. It
          must have one argument named "cloud_project_prefix" and one named
          "cloud_project_name."
  """
  return container_util.compound_project_name(args.cloud_project_prefix,
                                              args.cloud_project_name)
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
        and not args.use_cloud_bt and not args.cobalt_on_personal_cluster
        and not args.production_dir)
    start_cobalt_processes = ((test_dir in NEEDS_COBALT_PROCESSES)
        and not args.cobalt_on_personal_cluster and not args.production_dir)
    test_args = None
    if (test_dir == 'gtests_cloud_bt'):
      if not os.path.exists(bt_admin_service_account_credentials_file):
        print ('You must first create the file %s.' %
               bt_admin_service_account_credentials_file)
        print 'See the instructions in README.md.'
        return
      bigtable_project_name_from_args = _compound_project_name(args)
      if bigtable_project_name_from_args == '':
        print '--cloud_project_name must be specified'
        success = False
        break
      if args.bigtable_instance_name == '':
        print '--bigtable_instance_name must be specified'
        success = False
        break
      test_args = [
          "--bigtable_project_name=%s" % bigtable_project_name_from_args,
          "--bigtable_instance_name=%s" % args.bigtable_instance_name
      ]
      bigtable_project_name = bigtable_project_name_from_args
      bigtable_instance_name = args.bigtable_instance_name
    if (test_dir == 'e2e_tests'):
      analyzer_pk_pem_file=E2E_TEST_ANALYZER_PUBLIC_KEY_PEM
      analyzer_uri = "localhost:%d" % DEFAULT_ANALYZER_SERVICE_PORT
      report_master_uri = "localhost:%d" % DEFAULT_REPORT_MASTER_PORT
      shuffler_pk_pem_file=E2E_TEST_SHUFFLER_PUBLIC_KEY_PEM
      shuffler_uri = "localhost:%d" % DEFAULT_SHUFFLER_PORT
      if args.cobalt_on_personal_cluster or args.production_dir:
        if args.cobalt_on_personal_cluster and args.production_dir:
          print ("Do not specify both --production_dir and "
                 "-cobalt_on_personal_cluster.")
          success = False
          break
        public_uris = container_util.get_public_uris(args.cluster_name,
            args.cloud_project_prefix, args.cloud_project_name,
            args.cluster_zone)
        analyzer_uri = public_uris["analyzer"]
        report_master_uri = public_uris["report_master"]
        shuffler_uri = public_uris["shuffler"]
        if args.use_cloud_bt:
          # use_cloud_bt means to use local instances of the Cobalt processes
          # connected to a Cloud Bigtable. cobalt_on_personal_cluster means to
          # use Cloud instances of the Cobalt processes. These two options
          # are inconsistent.
          print ("Do not specify both --use_cloud_bt and "
                 "-cobalt_on_personal_cluster or --production_dir.")
          success = False
          break
      if args.cobalt_on_personal_cluster:
        analyzer_pk_pem_file=DEFAULT_ANALYZER_PUBLIC_KEY_PEM
        shuffler_pk_pem_file=DEFAULT_SHUFFLER_PUBLIC_KEY_PEM
      elif args.production_dir:
        pem_directory = os.path.abspath(args.production_dir)
        analyzer_pk_pem_file = os.path.join(pem_directory,
            'analyzer_public.pem')
        shuffler_pk_pem_file = os.path.join(pem_directory,
            'shuffler_public.pem')
      test_args = [
          "-analyzer_uri=%s" % analyzer_uri,
          "-analyzer_pk_pem_file=%s" % analyzer_pk_pem_file,
          "-shuffler_uri=%s" % shuffler_uri,
          "-shuffler_pk_pem_file=%s" % shuffler_pk_pem_file,
          "-report_master_uri=%s" % report_master_uri,
          ("-observation_querier_path=%s" %
              process_starter.OBSERVATION_QUERIER_PATH),
          "-test_app_path=%s" % process_starter.TEST_APP_PATH,
          "-sub_process_v=%d"%_verbose_count
      ]
      if (args.use_cloud_bt or args.cobalt_on_personal_cluster or
          args.production_dir):
        bigtable_project_name_from_args = _compound_project_name(args)
        test_args = test_args + [
          "-bigtable_tool_path=%s" % BIGTABLE_TOOL_PATH,
          "-bigtable_project_name=%s" % bigtable_project_name_from_args,
          "-bigtable_instance_name=%s" % args.bigtable_instance_name,
        ]
        bigtable_project_name = bigtable_project_name_from_args
        bigtable_instance_name = args.bigtable_instance_name
      if args.production_dir:
        # When running the end-to-end test against the production instance of
        # Cobalt, it may not be true that the Shuffler has been configured
        # to use a threshold of 100 so skip the part of the test that
        # verifies that.
        test_args = test_args + [
          "-do_shuffler_threshold_test=false",
        ]
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
  bigtable_project_name = ''
  bigtable_instance_name = ''
  if args.use_cloud_bt:
    bigtable_project_name = _compound_project_name(args)
    bigtable_instance_name = args.bigtable_instance_name
  process_starter.start_analyzer_service(port=args.port,
      bigtable_project_name=bigtable_project_name,
      bigtable_instance_name=bigtable_instance_name,
      # Because it makes the demo more interesting
      # we use verbose_count at least 3.
      verbose_count=max(3, _verbose_count))

def _start_report_master(args):
  bigtable_project_name = ''
  bigtable_instance_name = ''
  if args.use_cloud_bt:
    bigtable_project_name = _compound_project_name(args)
    bigtable_instance_name = args.bigtable_instance_name
  process_starter.start_report_master(port=args.port,
      bigtable_project_name=bigtable_project_name,
      bigtable_instance_name=bigtable_instance_name,
      cobalt_config_dir=args.cobalt_config_dir,
      verbose_count=_verbose_count)

def _start_test_app(args):
  analyzer_uri = "localhost:%d" % DEFAULT_ANALYZER_SERVICE_PORT
  shuffler_uri = "localhost:%d" % DEFAULT_SHUFFLER_PORT
  analyzer_public_key_pem = \
    DEFAULT_ANALYZER_PUBLIC_KEY_PEM
  shuffler_public_key_pem = \
    DEFAULT_ANALYZER_PUBLIC_KEY_PEM
  if args.production_dir:
    pem_directory = os.path.abspath(args.production_dir)
    analyzer_public_key_pem = os.path.join(pem_directory,
        'analyzer_public.pem')
    shuffler_public_key_pem = os.path.join(pem_directory,
        'shuffler_public.pem')
  if args.cobalt_on_personal_cluster or args.production_dir:
    public_uris = container_util.get_public_uris(args.cluster_name,
        args.cloud_project_prefix, args.cloud_project_name, args.cluster_zone)
    analyzer_uri = public_uris["analyzer"]
    shuffler_uri = public_uris["shuffler"]
  process_starter.start_test_app(shuffler_uri=shuffler_uri,
      analyzer_uri=analyzer_uri,
      analyzer_pk_pem_file=analyzer_public_key_pem,
      shuffler_pk_pem_file=shuffler_public_key_pem,
      cobalt_config_dir=args.cobalt_config_dir,
      project_id=args.project_id,
      # Because it makes the demo more interesting
      # we use verbose_count at least 3.
      verbose_count=max(3, _verbose_count))

def _start_report_client(args):
  report_master_uri = "localhost:%d" % DEFAULT_REPORT_MASTER_PORT
  if args.cobalt_on_personal_cluster or args.production_dir:
    public_uris = container_util.get_public_uris(args.cluster_name,
        args.cloud_project_prefix, args.cloud_project_name, args.cluster_zone)
    report_master_uri = public_uris["report_master"]
  process_starter.start_report_client(
      report_master_uri=report_master_uri,
      project_id=args.project_id,
      verbose_count=_verbose_count)

def _start_observation_querier(args):
  bigtable_project_name = ''
  bigtable_instance_name = ''
  if args.use_cloud_bt or args.production_dir:
    bigtable_project_name = _compound_project_name(args)
    bigtable_instance_name = args.bigtable_instance_name
  process_starter.start_observation_querier(
      bigtable_project_name=bigtable_project_name,
      bigtable_instance_name=bigtable_instance_name,
      verbose_count=_verbose_count)

def _generate_keys(args):
  path = os.path.join(OUT_DIR, 'tools', 'key_generator', 'key_generator')
  subprocess.check_call([path])

def _invoke_bigtable_tool(args, command):
  if not os.path.exists(bt_admin_service_account_credentials_file):
    print ('You must first create the file %s.' %
           bt_admin_service_account_credentials_file)
    print 'See the instructions in README.md.'
    return
  bigtable_project_name_from_args = _compound_project_name(args)
  if bigtable_project_name_from_args == '':
    print '--cloud_project_name must be specified'
    return
  if args.bigtable_instance_name == '':
    print '--bigtable_instance_name must be specified'
    return
  cmd = [BIGTABLE_TOOL_PATH,
         "-command", command,
         "-bigtable_project_name", bigtable_project_name_from_args,
         "-bigtable_instance_name", args.bigtable_instance_name]
  if command == 'delete_observations':
    if args.customer_id == 0:
      print '--customer_id must be specified'
      return
    if args.project_id == 0:
      print '--project_id must be specified'
      return
    if args.metric_id == 0:
      print '--metric_id must be specified'
      return
    cmd = cmd + ["-customer", str(args.customer_id),
                 "-project", str(args.project_id),
                 "-metric", str(args.metric_id)]
  elif command == 'delete_reports':
    if args.customer_id == 0:
      print '--customer_id must be specified'
      return
    if args.project_id == 0:
      print '--project_id must be specified'
      return
    if args.report_config_id == 0:
      print '--report_config_id must be specified'
      return
    cmd = cmd + ["-customer", str(args.customer_id),
                 "-project", str(args.project_id),
                 "-report_config", str(args.report_config_id)]
  subprocess.check_call(cmd)

def _provision_bigtable(args):
  _invoke_bigtable_tool(args, "create_tables")

def _delete_observations(args):
  _invoke_bigtable_tool(args, "delete_observations")

def _delete_reports(args):
  _invoke_bigtable_tool(args, "delete_reports")

def _deploy_show(args):
  container_util.display(args.cloud_project_prefix, args.cloud_project_name,
      args.cluster_zone, args.cluster_name)

def _deploy_authenticate(args):
  container_util.authenticate(args.cluster_name, args.cloud_project_prefix,
      args.cloud_project_name, args.cluster_zone)

def _deploy_build(args):
  container_util.build_all_docker_images(
      shuffler_config_file=args.shuffler_config_file,
      cobalt_config_dir=args.cobalt_config_dir)

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

def _parse_bool(bool_string):
  return bool_string.lower() in ['true', 't', 'y', 'yes', '1']

def _deploy_start(args):
  if args.job == 'shuffler':
    container_util.start_shuffler(
        args.cloud_project_prefix,
        args.cloud_project_name,
        args.cluster_zone, args.cluster_name,
        args.gce_pd_name,
        args.shuffler_static_ip,
        use_memstore=_parse_bool(args.shuffler_use_memstore),
        danger_danger_delete_all_data_at_startup=
            args.danger_danger_delete_all_data_at_startup)
  elif args.job == 'analyzer-service':
    if args.bigtable_instance_name == '':
        print '--bigtable_instance_name must be specified'
        return
    container_util.start_analyzer_service(
        args.cloud_project_prefix, args.cloud_project_name,
        args.cluster_zone, args.cluster_name,
        args.bigtable_instance_name,
        args.analyzer_service_static_ip)
  elif args.job == 'report-master':
    if args.bigtable_instance_name == '':
        print '--bigtable_instance_name must be specified'
        return
    container_util.start_report_master(
        args.cloud_project_prefix, args.cloud_project_name,
        args.cluster_zone, args.cluster_name,
        args.bigtable_instance_name,
        args.report_master_static_ip)
  else:
    print('Unknown job "%s". I only know how to start "shuffler", '
          '"analyzer-service" and "report-master".' % args.job)

def _deploy_stop(args):
  if args.job == 'shuffler':
    container_util.stop_shuffler(args.cloud_project_prefix,
        args.cloud_project_name, args.cluster_zone, args.cluster_name)
  elif args.job == 'analyzer-service':
    container_util.stop_analyzer_service(args.cloud_project_prefix,
        args.cloud_project_name, args.cluster_zone, args.cluster_name)
  elif args.job == 'report-master':
    container_util.stop_report_master(args.cloud_project_prefix,
        args.cloud_project_name, args.cluster_zone, args.cluster_name)
  else:
    print('Unknown job "%s". I only know how to stop "shuffler", '
          '"analyzer-service" and "report-master".' % args.job)

def _deploy_upload_secret_keys(args):
  container_util.create_analyzer_private_key_secret(args.cloud_project_prefix,
      args.cloud_project_name, args.cluster_zone, args.cluster_name,
      args.analyzer_private_key_pem)
  container_util.create_shuffler_private_key_secret(args.cloud_project_prefix,
      args.cloud_project_name, args.cluster_zone, args.cluster_name,
      args.shuffler_private_key_pem)

def _deploy_delete_secret_keys(args):
  container_util.delete_analyzer_private_key_secret(args.cloud_project_prefix,
      args.cloud_project_name, args.cluster_zone, args.cluster_name)
  container_util.delete_shuffler_private_key_secret(args.cloud_project_prefix,
      args.cloud_project_name, args.cluster_zone, args.cluster_name)

def _default_shuffler_config_file(cluster_settings):
  if cluster_settings['shuffler_config_file'] :
    return  os.path.join(THIS_DIR, cluster_settings['shuffler_config_file'])
  return SHUFFLER_DEMO_CONFIG_FILE

def _default_cobalt_config_dir(cluster_settings):
  if cluster_settings['cobalt_config_dir'] :
    return os.path.join(THIS_DIR, cluster_settings['cobalt_config_dir'])
  return DEMO_CONFIG_DIR

def _default_shuffler_use_memstore(cluster_settings):
  if cluster_settings['shuffler_use_memstore']:
    return cluster_settings['shuffler_use_memstore']
  return "false"

def _cluster_settings_from_json(cluster_settings, json_file_path):
  """ Reads cluster settings from a json file and adds them to a dictionary.

  Args:
    cluster_settings: A dictionary of cluster settings whose values will be
    overwritten by any corresponding values in the json file. Any values in
    the json file that do not correspond to a key in this dictionary will
    be ignored.

    json_file_path: The full path to a json file that must exist.
  """
  print ('The GKE cluster settings file being used is: %s.' % json_file_path)
  with open(json_file_path) as f:
    read_cluster_settings = json.load(f)
  for key in read_cluster_settings:
    if key in cluster_settings:
      cluster_settings[key] = read_cluster_settings[key]


def _add_cloud_access_args(parser, cluster_settings):
  parser.add_argument('--cloud_project_prefix',
      help='The prefix part of name of the Cloud project with which you wish '
           'to work. This is usually an organization domain name if your '
           'Cloud project is associated with one. Pass the empty string for no '
           'prefix. '
           'Default=%s.' %cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project with which you wish '
           'to work. This is the full project name if --cloud_project_prefix '
           'is empty. Otherwise the full project name is '
           '<cloud_project_prefix>:<cloud_project_name>. '
           'Default=%s' % cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])
  parser.add_argument('--cluster_name',
      help='The GKE "container cluster" within your Cloud project with which '
           'you wish to work. '
           'Default=%s' % cluster_settings['cluster_name'],
      default=cluster_settings['cluster_name'])
  parser.add_argument('--cluster_zone',
      help='The zone in which your GKE "container cluster" is located. '
           'Default=%s' % cluster_settings['cluster_zone'],
      default=cluster_settings['cluster_zone'])

def _add_gke_deployment_args(parser, cluster_settings):
  _add_cloud_access_args(parser, cluster_settings)
  parser.add_argument('--shuffler_static_ip',
      help='A static IP address that has been previously reserved on the GKE '
           'cluster for the Shuffler. '
           'Default=%s' % cluster_settings['shuffler_static_ip'],
      default=cluster_settings['shuffler_static_ip'])
  parser.add_argument('--report_master_static_ip',
      help='A static IP address that has been previously reserved on the GKE '
           'cluster for the Report Master. '
           'Default=%s' % cluster_settings['report_master_static_ip'],
      default=cluster_settings['report_master_static_ip'])
  parser.add_argument('--analyzer_service_static_ip',
      help='A static IP address that has been previously reserved on the GKE '
           'cluster for the Analyzer Service. '
           'Default=%s' % cluster_settings['analyzer_service_static_ip'],
      default=cluster_settings['analyzer_service_static_ip'])

def main():
  # We parse the command line flags twice. The first time we are looking
  # only for a particular flag, namely --production_dir. This first pass
  # will not print any help and will ignore all other flags.
  parser0 = argparse.ArgumentParser(add_help=False)
  parser0.add_argument('--production_dir', default='')
  args0, ignore = parser0.parse_known_args()
  # If the flag --production_dir is passed then it must be the path to
  # a directory containing a file named cluster.json. The contents of this
  # json file is used to set default values for many of the other flags. That
  # explains why we want to parse the flags twice.
  production_cluster_json_file = ''
  if args0.production_dir:
    production_cluster_json_file = os.path.join(args0.production_dir,
                                                'cluster.json')
    if not  os.path.exists(production_cluster_json_file):
       print ('File not found: %s.' % production_cluster_json_file)
       return

  # cluster_settings contains the default values for many of the flags.
  cluster_settings = {
    'cloud_project_prefix': '',
    'cloud_project_name': '',
    'cluster_name': '',
    'cluster_zone': '',
    'gce_pd_name': '',
    'bigtable_instance_name': '',
    'shuffler_static_ip' : '',
    'report_master_static_ip' : '',
    'analyzer_service_static_ip' : '',
    'shuffler_config_file': '',
    'cobalt_config_dir': '',
    'shuffler_use_memstore' : '',
  }
  if production_cluster_json_file:
    _cluster_settings_from_json(cluster_settings, production_cluster_json_file)
  elif os.path.exists(PERSONAL_CLUSTER_JSON_FILE):
    _cluster_settings_from_json(cluster_settings,
        PERSONAL_CLUSTER_JSON_FILE)

  # We also use the flag --production_dir to find the PEM files.
  analyzer_private_key_pem = DEFAULT_ANALYZER_PRIVATE_KEY_PEM
  shuffler_private_key_pem = DEFAULT_SHUFFLER_PRIVATE_KEY_PEM
  if args0.production_dir:
    pem_directory = os.path.abspath(args0.production_dir)
    analyzer_private_key_pem = os.path.join(pem_directory,
        'analyzer_private.pem')
    shuffler_private_key_pem = os.path.join(pem_directory,
        'shuffler_private.pem')

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

  # Even though the flag --production_dir was already parsed by parser0
  # above, we add the flag here too so that we can print help for it
  # and also so that the call to parser.parse_args() below will not complain
  # about --produciton_dir being unrecognized.
  production_dir_help = ("The path to a "
    "Cobalt production directory containing a 'cluster.json' file. The use of "
    "this flag in various commands implies that the command should be applied "
    "against the production Cobalt cluster associated with the specified "
    "directory. The contents of cluster.json will be used to set the "
    "default values for many of the other flags pertaining to production "
    "deployment. Additionally if there is a file named "
    "'bt_admin_service_account.json' in that directory then the environment "
    "variable GOOGLE_APPLICATION_CREDENTIALS will be set to the path to "
    "this file. Also the public and private key PEM files will be looked for "
    "in this directory. See README.md for details.")
  parser.add_argument('--production_dir', default='', help=production_dir_help)
  parent_parser.add_argument('--production_dir', default='',
      help=production_dir_help)

  parser.add_argument('--verbose',
    help='Be verbose (multiple times for more)',
    default=0, dest='verbose_count', action='count')
  parent_parser.add_argument('--verbose',
    help='Be verbose (multiple times for more)',
    default=0, dest='verbose_count', action='count')

  subparsers = parser.add_subparsers()

  ########################################################
  # setup command
  ########################################################
  sub_parser = subparsers.add_parser('setup', parents=[parent_parser],
    help='Sets up the build environment.')
  sub_parser.set_defaults(func=_setup)

  ########################################################
  # build command
  ########################################################
  sub_parser = subparsers.add_parser('build', parents=[parent_parser],
    help='Builds Cobalt.')
  sub_parser.set_defaults(func=_build)

  ########################################################
  # lint command
  ########################################################
  sub_parser = subparsers.add_parser('lint', parents=[parent_parser],
    help='Run language linters on all source files.')
  sub_parser.set_defaults(func=_lint)

  ########################################################
  # test command
  ########################################################
  sub_parser = subparsers.add_parser('test', parents=[parent_parser],
    help='Runs Cobalt tests. You must build first.')
  sub_parser.set_defaults(func=_test)
  sub_parser.add_argument('--tests', choices=TEST_FILTERS,
      help='Specify a subset of tests to run. Default=all',
      default='all')
  sub_parser.add_argument('-use_cloud_bt',
      help='Causes the end-to-end tests to run using local instances of the '
      'Cobalt processes connected to an instance of Cloud Bigtable. Otherwise '
      'a local instance of the Bigtable Emulator will be used.',
      action='store_true')
  sub_parser.add_argument('-cobalt_on_personal_cluster',
      help='Causes the end-to-end tests to run using the instance of Cobalt '
      'deployed on your personal GKE cluster. Otherwise local instances of the '
      'Cobalt processes are used. This option and -use_cloud_bt are mutually '
      'inconsistent. Do not use both at the same time. Also this option and '
      '--production_dir or mutually inconsistent because --production_dir '
      'implies that the end-to-end tests should be run against the specified '
      'production instance of Cobalt.',
      action='store_true')
  _add_cloud_access_args(sub_parser, cluster_settings)
  sub_parser.add_argument('--bigtable_instance_name',
      help='Specify a Cloud Bigtable instance within the specified Cloud'
      ' project against which to run some of the tests.'
      ' Only used for the cloud_bt tests and e2e tests when either '
      '-use_cloud_bt or -cobalt_on_personal_cluster are specified.'
      ' default=%s'%cluster_settings['bigtable_instance_name'],
      default=cluster_settings['bigtable_instance_name'])

  ########################################################
  # clean command
  ########################################################
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
  sub_parser.add_argument('-use_cloud_bt',
      help='Causes the Analyzer service to connect to an instance of Cloud '
           'Bigtable. Otherwise a local instance of the Bigtable Emulator will '
           'be used.', action='store_true')
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the name of the Cloud project containing the '
           'Bigtable instance to which the Analyzer service will connect. Only '
           'used if -use_cloud_bt is set. '
           'This is usually an organization domain name if your Cloud project '
           'is associated with one. Pass the empty string for no prefix. '
           'Default=%s.'%cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project containing the '
           'Bigtable instance to which the Analyzer service will connect. '
           'Only used if -use_cloud_bt is set. This is the full project '
           'name if --cloud_project_prefix is empty. Otherwise the full '
           'project name is <cloud_project_prefix>:<cloud_project_name>. '
           'default=%s'%cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--bigtable_instance_name',
      help='Specify a Cloud Bigtable instance within the specified Cloud '
      'project against which to query. Only used if -use_cloud_bt is set. '
      'default=%s'%cluster_settings['bigtable_instance_name'],
      default=cluster_settings['bigtable_instance_name'])
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
  sub_parser.add_argument('-use_cloud_bt',
      help='Causes the Report Master to connect to an instance of Cloud '
           'Bigtable. Otherwise a local instance of the Bigtable Emulator will '
           'be used.', action='store_true')
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the name of the Cloud project containing the '
           'Bigtable instance to which the Report Master will connect. Only '
           'used if -use_cloud_bt is set. '
           'This is usually an organization domain name if your Cloud project '
           'is associated with one. Pass the empty string for no prefix. '
           'Default=%s.'%cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project containing the '
           'Bigtable instance to which the Report Master will connect. '
           'Only used if -use_cloud_bt is set. This is the full project '
           'name if --cloud_project_prefix is empty. Otherwise the full '
           'project name is <cloud_project_prefix>:<cloud_project_name>. '
           'default=%s'%cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--bigtable_instance_name',
      help='Specify a Cloud Bigtable instance within the specified Cloud '
      'project against which to query. Only used if -use_cloud_bt is set. '
      'default=%s'%cluster_settings['bigtable_instance_name'],
      default=cluster_settings['bigtable_instance_name'])
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
  sub_parser.add_argument('-cobalt_on_personal_cluster',
      help='Causes the test_app to run using an instance of Cobalt '
      'deployed in Google Container Engine. Otherwise local instances of the '
      'Cobalt processes are used.',
      action='store_true')
  default_cobalt_config_dir = _default_cobalt_config_dir(cluster_settings)
  sub_parser.add_argument('--cobalt_config_dir',
      help='Path of directory containing Cobalt configuration files. '
           'Default=%s' % default_cobalt_config_dir,
      default=default_cobalt_config_dir)
  _add_cloud_access_args(sub_parser, cluster_settings)
  sub_parser.add_argument('--project_id',
    help='Specify the Cobalt project ID with which you wish to work. '
         'Must be in the range [1, 99]. Default = 1.',
    default=1)

  sub_parser = start_subparsers.add_parser('report_client',
      parents=[parent_parser], help='Start the Cobalt report client.')
  sub_parser.set_defaults(func=_start_report_client)
  _add_cloud_access_args(sub_parser, cluster_settings)
  sub_parser.add_argument('-cobalt_on_personal_cluster',
      help='Causes the report_client to query the instance of ReportMaster '
      'in your personal cluster rather than one running locally. If '
      '--production_dir is also specified it takes precedence and causes '
      'the report_client to query the instance of ReportMaster in the '
      'specified production cluster.',
      action='store_true')
  sub_parser.add_argument('--project_id',
    help='Specify the Cobalt project ID from which you wish to query. '
         'Default = 1.',
    default=1)

  sub_parser = start_subparsers.add_parser('observation_querier',
      parents=[parent_parser], help='Start the Cobalt ObservationStore '
                                    'querying tool.')
  sub_parser.set_defaults(func=_start_observation_querier)
  sub_parser.add_argument('-use_cloud_bt',
      help='Causes the query to be performed against an instance of Cloud '
      'Bigtable. Otherwise a local instance of the Bigtable Emulator will be '
      'used.', action='store_true')
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the name of the Cloud project containing the '
           'Bigtable instance against which to '
           'query. Only used if -use_cloud_bt is set. This is '
           'usually an organization domain name if your Cloud project is '
           'associated with one. Pass the empty string for no prefix. '
           'Default=%s.'%cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project containing the '
           'Bigtable instance against which to '
           'query. Only used if -use_cloud_bt is set. This is the full project '
           'name if --cloud_project_prefix is empty. Otherwise the full '
           'project name is <cloud_project_prefix>:<cloud_project_name>. '
           'default=%s'%cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--bigtable_instance_name',
      help='Specify a Cloud Bigtable instance within the specified Cloud '
      'project against which to query. Only used if -use_cloud_bt is set. '
      'default=%s'%cluster_settings['bigtable_instance_name'],
      default=cluster_settings['bigtable_instance_name'])

  sub_parser = start_subparsers.add_parser('bigtable_emulator',
    parents=[parent_parser],
    help='Start the Bigtable Emulator running locally.')
  sub_parser.set_defaults(func=_start_bigtable_emulator)

  sub_parser = subparsers.add_parser('keygen', parents=[parent_parser],
    help='Generate new public/private key pairs.')
  sub_parser.set_defaults(func=_generate_keys)

  ########################################################
  # bigtable command
  ########################################################
  bigtable_parser = subparsers.add_parser('bigtable',
    help='Perform an operation on your personal Cloud Bigtable cluster.')
  bigtable_subparsers = bigtable_parser.add_subparsers()

  sub_parser = bigtable_subparsers.add_parser('provision',
    parents=[parent_parser],
    help="Create Cobalt's Cloud BigTables if they don't exit.")
  sub_parser.set_defaults(func=_provision_bigtable)
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the name of the Cloud project containing the '
           'Bigtable instance to be provisioned. This is '
           'usually an organization domain name if your Cloud project is '
           'associated with one. Pass the empty string for no prefix. '
           'Default=%s.'%cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project containing the '
           'Bigtable instance to be provisioned. This is the full project '
           'name if --cloud_project_prefix is empty. Otherwise the full '
           'project name is <cloud_project_prefix>:<cloud_project_name>. '
           'default=%s'%cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--bigtable_instance_name',
    help='Specify the Cloud Bigtable instance within the specified Cloud'
    ' project that is to be provisioned. '
    'default=%s'%cluster_settings['bigtable_instance_name'],
    default=cluster_settings['bigtable_instance_name'])

  sub_parser = bigtable_subparsers.add_parser('delete_observations',
    parents=[parent_parser],
    help='**WARNING: Permanently delete data from Cobalt\'s Observation '
    'store. **')
  sub_parser.set_defaults(func=_delete_observations)
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the name of the Cloud project containing the '
           'Bigtable instance from which all Observation data will be '
           'permanently deleted. This is '
           'usually an organization domain name if your Cloud project is '
           'associated with one. Pass the empty string for no prefix. '
           'Default=%s.'%cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project containing the '
           'Bigtable instance from which all Observation data will be '
           'permanently deleted. This is the full project '
           'name if --cloud_project_prefix is empty. Otherwise the full '
           'project name is <cloud_project_prefix>:<cloud_project_name>. '
           'default=%s'%cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--bigtable_instance_name',
    help='Specify the Cloud Bigtable instance within the specified Cloud'
    ' project from which all Observation data will be permanently deleted. '
    'default=%s'%cluster_settings['bigtable_instance_name'],
    default=cluster_settings['bigtable_instance_name'])
  sub_parser.add_argument('--customer_id',
    help='Specify the Cobalt customer ID from which you wish to delete '
         'observations. Required.',
    default=0)
  sub_parser.add_argument('--project_id',
    help='Specify the Cobalt project ID from which you wish to delete '
         'observations. Required.',
    default=0)
  sub_parser.add_argument('--metric_id',
    help='Specify the Cobalt metric ID from which you wish to delete '
         'observations. Required.',
    default=0)

  sub_parser = bigtable_subparsers.add_parser('delete_reports',
    parents=[parent_parser],
    help='**WARNING: Permanently delete data from Cobalt\'s Report '
    'store. **')
  sub_parser.set_defaults(func=_delete_reports)
  sub_parser.add_argument('--cloud_project_prefix',
      help='The prefix part of the name of the Cloud project containing the '
           'Bigtable instance from which all Report data will be '
           'permanently deleted. This is '
           'usually an organization domain name if your Cloud project is '
           'associated with one. Pass the empty string for no prefix. '
           'Default=%s.'%cluster_settings['cloud_project_prefix'],
      default=cluster_settings['cloud_project_prefix'])
  sub_parser.add_argument('--cloud_project_name',
      help='The main part of the name of the Cloud project containing the '
           'Bigtable instance from which all Report data will be '
           'permanently deleted. This is the full project '
           'name if --cloud_project_prefix is empty. Otherwise the full '
           'project name is <cloud_project_prefix>:<cloud_project_name>. '
           'default=%s'%cluster_settings['cloud_project_name'],
      default=cluster_settings['cloud_project_name'])
  sub_parser.add_argument('--bigtable_instance_name',
    help='Specify the Cloud Bigtable instance within the specified Cloud'
    ' project from which all Report data will be permanently deleted. '
    'default=%s'%cluster_settings['bigtable_instance_name'],
    default=cluster_settings['bigtable_instance_name'])
  sub_parser.add_argument('--customer_id',
    help='Specify the Cobalt customer ID from which you wish to delete '
         'reports. Required.',
    default=0)
  sub_parser.add_argument('--project_id',
    help='Specify the Cobalt project ID from which you wish to delete '
         'reports. Required.',
    default=0)
  sub_parser.add_argument('--report_config_id',
    help='Specify the Cobalt report config ID for which you wish to delete '
         'all report data. Required.',
    default=0)

  ########################################################
  # deploy command
  ########################################################
  deploy_parser = subparsers.add_parser('deploy',
    parents=[parent_parser],
    help='Build Docker containers. Push to Container Regitry. Deploy to GKE.')
  deploy_subparsers = deploy_parser.add_subparsers()

  sub_parser = deploy_subparsers.add_parser('show',
      parents=[parent_parser], help='Display information about currently '
      'deployed jobs on GKE, including their public URIs.')
  sub_parser.set_defaults(func=_deploy_show)
  _add_gke_deployment_args(sub_parser, cluster_settings)

  sub_parser = deploy_subparsers.add_parser('authenticate',
      parents=[parent_parser], help='Refresh your authentication token if '
      'necessary. Also associates your local computer with a particular '
      'GKE cluster to which you will be deploying.')
  sub_parser.set_defaults(func=_deploy_authenticate)
  _add_gke_deployment_args(sub_parser, cluster_settings)

  sub_parser = deploy_subparsers.add_parser('build',
      parents=[parent_parser], help='Rebuild all Docker images. '
          'You must have the Docker daemon running.')
  sub_parser.set_defaults(func=_deploy_build)
  default_shuffler_config_file = _default_shuffler_config_file(
      cluster_settings)
  sub_parser.add_argument('--shuffler_config_file',
      help='Path to the Shuffler configuration file. '
           'Default=%s' % default_shuffler_config_file,
      default=default_shuffler_config_file)
  default_cobalt_config_dir = _default_cobalt_config_dir(cluster_settings)
  sub_parser.add_argument('--cobalt_config_dir',
      help='Path of directory containing Cobalt configuration files. '
           'Default=%s' % default_cobalt_config_dir,
      default=default_cobalt_config_dir)

  sub_parser = deploy_subparsers.add_parser('push',
      parents=[parent_parser], help='Push a Docker image to the Google'
          'Container Registry.')
  sub_parser.set_defaults(func=_deploy_push)
  sub_parser.add_argument('--job',
      help='The job you wish to push. Valid choices are "shuffler", '
           '"analyzer-service", "report-master". Required.')
  _add_gke_deployment_args(sub_parser, cluster_settings)

  sub_parser = deploy_subparsers.add_parser('start',
      parents=[parent_parser], help='Start one of the jobs on GKE.')
  sub_parser.set_defaults(func=_deploy_start)
  _add_gke_deployment_args(sub_parser, cluster_settings)
  sub_parser.add_argument('--job',
      help='The job you wish to start. Valid choices are "shuffler", '
           '"analyzer-service", "report-master". Required.')
  sub_parser.add_argument('--bigtable_instance_name',
      help='Specify a Cloud Bigtable instance within the specified Cloud '
           'project that the Analyzer should connect to. This is required '
           'if and only if you are starting one of the two Analyzer jobs. '
           'Default=%s' % cluster_settings['bigtable_instance_name'],
      default=cluster_settings['bigtable_instance_name'])
  sub_parser.add_argument('--gce_pd_name',
      help='The name of a GCE persistent disk. This is used only when starting '
           'the Shuffler. The disk must already have been created in the same '
           'Cloud project in which the Shuffler is being deployed. '
           'Default=%s' % cluster_settings['gce_pd_name'],
      default=cluster_settings['gce_pd_name'])
  default_shuffler_use_memstore = _default_shuffler_use_memstore(
      cluster_settings)
  sub_parser.add_argument('--shuffler-use-memstore',
      default=default_shuffler_use_memstore,
      help=('When starting the Shuffler, should the Suffler use its in-memory '
            'data store rather than a persistent datastore? Default=%s.' %
            default_shuffler_use_memstore))
  sub_parser.add_argument('-danger_danger_delete_all_data_at_startup',
      help='When starting the Shuffler, should all of the Observations '
      'collected during previous runs of the Shuffler be permanently and '
      'irrecoverably deleted from the Shuffler\'s store upon startup?',
      action='store_true')

  sub_parser = deploy_subparsers.add_parser('stop',
      parents=[parent_parser], help='Stop one of the jobs on GKE.')
  sub_parser.set_defaults(func=_deploy_stop)
  _add_gke_deployment_args(sub_parser, cluster_settings)
  sub_parser.add_argument('--job',
      help='The job you wish to stop. Valid choices are "shuffler", '
           '"analyzer-service", "report-master". Required.')

  sub_parser = deploy_subparsers.add_parser('upload_secret_keys',
      parents=[parent_parser], help='Creates |secret| objects in the '
      'cluster to store the private keys for the Analyzer and the Shuffler. '
      'The private keys must first be generated using the "generate_keys" '
      'command (once for the Analyzer and once for the Shuffler). This must be '
      'done at least once before starting the Analyzer Service or the '
      'Shuffler. To replace the keys first delete the old ones using the '
      '"deploy delete_secret_keys" command.')
  sub_parser.set_defaults(func=_deploy_upload_secret_keys)
  _add_gke_deployment_args(sub_parser, cluster_settings)
  sub_parser.add_argument('--analyzer_private_key_pem',
      default=analyzer_private_key_pem)
  sub_parser.add_argument('--shuffler_private_key_pem',
      default=shuffler_private_key_pem)

  sub_parser = deploy_subparsers.add_parser('delete_secret_keys',
      parents=[parent_parser], help='Deletes the |secret| objects in the '
      'cluster that were created using the "deploy upload_secret_keys" '
      'command.')
  sub_parser.set_defaults(func=_deploy_delete_secret_keys)
  _add_gke_deployment_args(sub_parser, cluster_settings)

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

  global bt_admin_service_account_credentials_file
  bt_admin_service_account_credentials_file = \
      PERSONAL_BT_ADMIN_SERVICE_ACCOUNT_CREDENTIALS_FILE
  if args0.production_dir:
    bt_admin_service_account_credentials_file = os.path.abspath(
        os.path.join(args0.production_dir, 'bt_admin_service_account.json'))

  os.environ["GOOGLE_APPLICATION_CREDENTIALS"] = \
      bt_admin_service_account_credentials_file

  os.environ["GRPC_DEFAULT_SSL_ROOTS_FILE_PATH"] = os.path.abspath(
      os.path.join(SYSROOT_DIR, 'share', 'grpc', 'roots.pem'))

  os.environ["GOROOT"] = "%s/golang" % SYSROOT_DIR

  return args.func(args)


if __name__ == '__main__':
  sys.exit(main())
