# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file includes macros to make it easier to log in a standardized format
# so that we can create logs-based metrics for stackdriver.
#
# https://cloud.google.com/logging/docs/logs-based-metrics/

"""A library to help with building/deploying production cobalt packages."""

import subprocess
import tempfile
import shutil
import os

import container_util

COBALT_REPO_CLONE_URL = "https://fuchsia.googlesource.com/cobalt"

def _cobaltb(*args):
  cmd = ['./cobaltb.py']
  cmd.extend(args)
  subprocess.check_call(cmd)

def _select_git_revision():
  tags = subprocess.check_output(['git', 'tag', '-l',
    '--sort=-version:refname']).strip().split('\n')[:5]
  tags.append('HEAD')

  while True:
    print
    print
    print('Which version would you like to build for?')
    for i, tag in enumerate(tags):
      print('({}) {}'.format(i, tag))

    selection = raw_input('? ')
    try:
      selection = int(selection)
      if selection < len(tags):
        return tags[selection]
    except:
      print("Invalid selection")

def build_and_push_production_docker_images(cloud_project_name, production_dir,
    git_revision):
  """ Builds and pushes production-ready docker images from a clean git repo.
  cloud_project_name {sring}: For example "fuchsia-cobalt". The name is used
      when forming the URI to the image in the registry and also the bigtable
      project name.
  production_dir {string}: The directory of the production config files.
  git_revision {string}: A git revision passed in from the command line, if none
    is provided, the user will be prompted to select one.
    latest will be used.
  """

  clean_repo_dir = tempfile.mkdtemp('-cobalt-production-build')

  try:
    subprocess.check_call(['git', 'clone', COBALT_REPO_CLONE_URL,
      clean_repo_dir])

    wd = os.getcwd()
    os.chdir(clean_repo_dir)

    if not git_revision:
      git_revision = _select_git_revision()

    if git_revision is 'HEAD':
      git_revision = subprocess.check_output(['git', 'rev-parse',
        'HEAD']).strip()

    subprocess.check_call(['git', 'checkout', git_revision])
    describe = subprocess.check_output(['git', 'describe']).strip()
    full_rev = subprocess.check_output(['git', 'rev-parse', 'HEAD']).strip()

    _cobaltb('setup')
    _cobaltb('clean', '--full')
    _cobaltb('build')
    _cobaltb('test')

    p = subprocess.Popen(['./cobaltb.py', 'deploy', 'build',
                           '--production_dir=%s' % production_dir],
                           stdin=subprocess.PIPE)
    p.communicate('yes')

    tags_to_apply = ['latest', full_rev]
    if describe is not git_revision:
      tags_to_apply.append(describe)
    subrev = ''
    # This will construct a series of tags based on the version. e.g. if the
    # version is v1.2.3, it would create the tags 'v1', 'v1.2', and 'v1.2.3'
    for part in git_revision.split('.'):
      if subrev is not '':
        subrev += '.'
      subrev += part
      tags_to_apply.append(subrev)

    for tag in tags_to_apply:
      container_util.push_shuffler_to_container_registry(cloud_project_prefix,
          cloud_project_name, tag)
      container_util.push_analyzer_service_to_container_registry(cloud_project_prefix,
          cloud_project_name, tag)
      container_util.push_report_master_to_container_registry(cloud_project_prefix,
          cloud_project_name, tag)

    os.chdir(wd)

    return full_rev
  finally:
    raw_input("Press enter to finish build and delete directory...")
    print("Cleaning up")
    shutil.rmtree(clean_repo_dir)
