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

"""A library with functions to help work with Docker, Kubernetes and GKE."""

import fileinput
import os
import re
import shutil
import string
import subprocess
import sys

import process_starter
from process_starter import ANALYZER_PRIVATE_KEY_PEM_NAME
from process_starter import ANALYZER_SERVICE_PATH
from process_starter import DEFAULT_ANALYZER_SERVICE_PORT
from process_starter import DEFAULT_REPORT_MASTER_PORT
from process_starter import DEFAULT_SHUFFLER_PORT
from process_starter import DEMO_CONFIG_DIR
from process_starter import REPORT_MASTER_PATH
from process_starter import SHUFFLER_PRIVATE_KEY_PEM_NAME
from process_starter import SHUFFLER_CONFIG_FILE
from process_starter import SHUFFLER_PATH

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.join(THIS_DIR, os.pardir)
OUT_DIR = os.path.abspath(os.path.join(SRC_ROOT_DIR, 'out'))
SYS_ROOT_DIR = os.path.join(SRC_ROOT_DIR, 'sysroot')

# The URI of the Google Container Registry.
CONTAINER_REGISTRY_URI = 'us.gcr.io'

# Dockerfile/Kubernetes source file paths
KUBE_SRC_DIR = os.path.join(SRC_ROOT_DIR, 'kubernetes')
COBALT_COMMON_DOCKER_FILE= os.path.join(KUBE_SRC_DIR, 'cobalt_common',
    'Dockerfile')
ANALYZER_SERVICE_DOCKER_FILE = os.path.join(KUBE_SRC_DIR, 'analyzer_service',
    'Dockerfile')
REPORT_MASTER_DOCKER_FILE = os.path.join(KUBE_SRC_DIR, 'report_master',
    'Dockerfile')
SHUFFLER_DOCKER_FILE = os.path.join(KUBE_SRC_DIR, 'shuffler',
    'Dockerfile')

# Kubernetes deployment yaml template files with replaceable tokens.
ANALYZER_SERVICE_DEPLOYMENT_YAML = 'analyzer_service_deployment.yaml'
ANALYZER_SERVICE_DEPLOYMENT_TEMPLATE_FILE = os.path.join(KUBE_SRC_DIR,
    'analyzer_service', ANALYZER_SERVICE_DEPLOYMENT_YAML)
REPORT_MASTER_DEPLOYMENT_YAML = 'report_master_deployment.yaml'
REPORT_MASTER_DEPLOYMENT_TEMPLATE_FILE = os.path.join(KUBE_SRC_DIR,
    'report_master', REPORT_MASTER_DEPLOYMENT_YAML)
SHUFFLER_DEPLOYMENT_YAML = 'shuffler_deployment.yaml'
SHUFFLER_DEPLOYMENT_TEMPLATE_FILE = os.path.join(KUBE_SRC_DIR, 'shuffler',
    SHUFFLER_DEPLOYMENT_YAML)

# Kubernetes output directory
KUBE_OUT_DIR = os.path.join(OUT_DIR, 'kubernetes')

# Post-processed kubernetes deployment yaml files. These have had their tokens
# replaced and are ready to be used by "kubectl create"
ANALYZER_SERVICE_DEPLOYMENT_FILE = os.path.join(KUBE_OUT_DIR,
    ANALYZER_SERVICE_DEPLOYMENT_YAML)
REPORT_MASTER_DEPLOYMENT_FILE = os.path.join(KUBE_OUT_DIR,
    REPORT_MASTER_DEPLOYMENT_YAML)
SHUFFLER_DEPLOYMENT_FILE = os.path.join(KUBE_OUT_DIR, SHUFFLER_DEPLOYMENT_YAML)

# Docker image deployment directories
COBALT_COMMON_DOCKER_BUILD_DIR = os.path.join(KUBE_OUT_DIR,
    'cobalt_common')
ANALYZER_SERVICE_DOCKER_BUILD_DIR = os.path.join(KUBE_OUT_DIR,
    'analyzer_service')
REPORT_MASTER_DOCKER_BUILD_DIR = os.path.join(KUBE_OUT_DIR,
    'report_master')
SHUFFLER_DOCKER_BUILD_DIR = os.path.join(KUBE_OUT_DIR,
    'shuffler')

# Docker Image Names
COBALT_COMMON_IMAGE_NAME = "cobalt-common"
ANALYZER_SERVICE_IMAGE_NAME = "analyzer-service"
REPORT_MASTER_IMAGE_NAME = "report-master"
SHUFFLER_IMAGE_NAME = "shuffler"

COBALT_COMMON_SO_FILES = [os.path.join(SYS_ROOT_DIR, 'lib', f) for f in
    ["libgoogleapis.so",
     "libgrpc.so.1",
     "libgrpc++.so.1",
     "libprotobuf.so.10",
     "libunwind.so.1",
    ]]

ROOTS_PEM = os.path.join(SYS_ROOT_DIR, 'share', 'grpc', 'roots.pem')

ANALYZER_CONFIG_FILE_NAMES = [
    "registered_encodings.txt",
    "registered_metrics.txt",
    "registered_reports.txt"
]

ANALYZER_PRIVATE_KEY_SECRET_NAME = "analyzer-private-key"
SHUFFLER_PRIVATE_KEY_SECRET_NAME = "shuffler-private-key"

def _ensure_dir(dir_path):
  """Ensures that the directory at |dir_path| exists. If not it is created.

  Args:
    dir_path{string} The path to a directory. If it does not exist it will be
    created.
  """
  if not os.path.exists(dir_path):
    os.makedirs(dir_path)

def _set_contents_of_dir(dir_name, files_to_copy):
  shutil.rmtree(dir_name, ignore_errors=True)
  os.makedirs(dir_name)
  for f in files_to_copy:
    shutil.copy(f, dir_name)

def _build_analyzer_config_file_list(cobalt_config_dir):
  return [os.path.join(cobalt_config_dir, f) for f in
          ANALYZER_CONFIG_FILE_NAMES]

def _build_cobalt_common_deploy_dir():
  files_to_copy = [COBALT_COMMON_DOCKER_FILE, ROOTS_PEM] +  \
                  COBALT_COMMON_SO_FILES
  _set_contents_of_dir(COBALT_COMMON_DOCKER_BUILD_DIR, files_to_copy)

def _build_analyzer_service_deploy_dir():
  files_to_copy = [ANALYZER_SERVICE_DOCKER_FILE, ANALYZER_SERVICE_PATH]
  _set_contents_of_dir(ANALYZER_SERVICE_DOCKER_BUILD_DIR, files_to_copy)

def _build_report_master_deploy_dir(cobalt_config_dir):
  files_to_copy = [REPORT_MASTER_DOCKER_FILE, REPORT_MASTER_PATH] + \
      _build_analyzer_config_file_list(cobalt_config_dir)
  _set_contents_of_dir(REPORT_MASTER_DOCKER_BUILD_DIR, files_to_copy)

def _build_shuffler_deploy_dir(config_file):
  files_to_copy = [SHUFFLER_DOCKER_FILE, SHUFFLER_PATH, config_file]
  _set_contents_of_dir(SHUFFLER_DOCKER_BUILD_DIR, files_to_copy)

def _build_docker_image(image_name, deploy_dir, extra_args=None):
  cmd = ["docker", "build"]
  if extra_args:
    cmd = cmd + extra_args
  cmd = cmd + ["-t", image_name, deploy_dir]
  subprocess.check_call(cmd)

def build_all_docker_images(shuffler_config_file=SHUFFLER_CONFIG_FILE,
      cobalt_config_dir=DEMO_CONFIG_DIR):
  _build_cobalt_common_deploy_dir()
  _build_docker_image(COBALT_COMMON_IMAGE_NAME,
                      COBALT_COMMON_DOCKER_BUILD_DIR)

  _build_analyzer_service_deploy_dir()
  _build_docker_image(ANALYZER_SERVICE_IMAGE_NAME,
                      ANALYZER_SERVICE_DOCKER_BUILD_DIR)

  _build_report_master_deploy_dir(cobalt_config_dir)
  _build_docker_image(REPORT_MASTER_IMAGE_NAME,
                      REPORT_MASTER_DOCKER_BUILD_DIR)

  # Pass the full path of the config file to be copied into the deoply dir.
  _build_shuffler_deploy_dir(shuffler_config_file)

  # But pass only the basename to be found by Docker and copied into the image.
  config_file_name = os.path.basename(shuffler_config_file)
  _build_docker_image(SHUFFLER_IMAGE_NAME, SHUFFLER_DOCKER_BUILD_DIR,
      extra_args=["--build-arg", "config_file=%s"%config_file_name])

def _image_registry_uri(cloud_project_prefix, cloud_project_name, image_name):
  if not cloud_project_prefix:
    return "%s/%s/%s" % (CONTAINER_REGISTRY_URI, cloud_project_name, image_name)
  return "%s/%s/%s/%s" % (CONTAINER_REGISTRY_URI, cloud_project_prefix,
                          cloud_project_name, image_name)

def _push_to_container_registry(cloud_project_prefix, cloud_project_name,
                                image_name):
  registry_tag = _image_registry_uri(cloud_project_prefix, cloud_project_name,
                                     image_name)
  subprocess.check_call(["docker", "tag", image_name, registry_tag])
  subprocess.check_call(["gcloud", "docker", "--", "push", registry_tag])

def push_analyzer_service_to_container_registry(cloud_project_prefix,
                                               cloud_project_name):
  _push_to_container_registry(cloud_project_prefix, cloud_project_name,
                              ANALYZER_SERVICE_IMAGE_NAME)

def push_report_master_to_container_registry(cloud_project_prefix,
                                             cloud_project_name):
  _push_to_container_registry(cloud_project_prefix, cloud_project_name,
                              REPORT_MASTER_IMAGE_NAME)

def push_shuffler_to_container_registry(cloud_project_prefix,
                                        cloud_project_name):
  _push_to_container_registry(cloud_project_prefix, cloud_project_name,
                              SHUFFLER_IMAGE_NAME)

# A special value recognized by the function _replace_tokens_in_template. If
# a line of a template file contains a token $$FOO$$ and if the provided
# token_replacements dictionary contains a value for the token $$FOO$$ that
# is equal to the DELETE_LINE_INDICATOR then _replace_tokens_in_template will
# delete the line from the generated output.
DELETE_LINE_INDICATOR = '$$$$DELETE_LINE$$$'

def _replace_tokens_in_template(template_file, out_file, token_replacements):
  _ensure_dir(os.path.dirname(out_file))
  used_tokens = set()
  with open(out_file, 'w+b') as f:
    for line in fileinput.input(template_file):
      delete_this_line = False
      for token in token_replacements:
        if string.find(line, token) != -1:
          used_tokens.add(token)
          if token_replacements[token] == DELETE_LINE_INDICATOR:
            delete_this_line = True
            break
          else:
            line = line.replace(token, token_replacements[token])
      if not delete_this_line:
        f.write(line)
  for token in token_replacements:
    if not token in used_tokens:
      raise Exception("The token %s was never used." % token)

def compound_project_name(cloud_project_prefix, cloud_project_name):
  if not cloud_project_prefix:
    return cloud_project_name
  return "%s:%s"%(cloud_project_prefix, cloud_project_name)

def _form_context_name(cloud_project_prefix, cloud_project_name, cluster_zone,
                       cluster_name):
  return "gke_%s_%s_%s" % (compound_project_name(cloud_project_prefix,
      cloud_project_name), cluster_zone, cluster_name)

def _create_secret_from_file(secret_name, data_key, file_path, context):
  subprocess.call(["kubectl", "create", "secret", "generic", secret_name,
    "--from-file", "%s=%s"%(data_key, file_path),
    "--context", context])

def _delete_secret(secret_name, context):
  subprocess.call(["kubectl", "delete", "secret",  secret_name,
                   "--context", context])

def create_analyzer_private_key_secret(cloud_project_prefix,
    cloud_project_name, cluster_zone, cluster_name, path_to_pem):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
      cluster_zone, cluster_name)
  _create_secret_from_file(ANALYZER_PRIVATE_KEY_SECRET_NAME,
                           ANALYZER_PRIVATE_KEY_PEM_NAME,
                           path_to_pem, context)

def create_shuffler_private_key_secret(cloud_project_prefix,
    cloud_project_name, cluster_zone, cluster_name, path_to_pem):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
      cluster_zone, cluster_name)
  _create_secret_from_file(SHUFFLER_PRIVATE_KEY_SECRET_NAME,
                           SHUFFLER_PRIVATE_KEY_PEM_NAME,
                           path_to_pem, context)

def delete_analyzer_private_key_secret(cloud_project_prefix,
    cloud_project_name, cluster_zone, cluster_name):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
      cluster_zone, cluster_name)
  _delete_secret(ANALYZER_PRIVATE_KEY_SECRET_NAME, context)

def delete_shuffler_private_key_secret(cloud_project_prefix,
    cloud_project_name, cluster_zone, cluster_name):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
      cluster_zone, cluster_name)
  _delete_secret(SHUFFLER_PRIVATE_KEY_SECRET_NAME, context)

def _start_gke_service(deployment_template_file, deployment_file,
                       token_substitutions, context):
  # Generate the kubernetes deployment file by performing token replacement.
  _replace_tokens_in_template(deployment_template_file, deployment_file,
                              token_substitutions)

  # Invoke "kubectl create" on the deployment file we just generated.
  subprocess.check_call(["kubectl", "create", "-f", deployment_file,
                         "--context", context])

def start_analyzer_service(cloud_project_prefix,
                           cloud_project_name,
                           cluster_zone, cluster_name,
                           bigtable_instance_name,
                           static_ip_address):
  """ Starts the analyzer-service deployment and service.
  cloud_project_prefix {sring}: For example "google.com"
  cloud_project_name {sring}: For example "shuffler-test". The prefix and
      name are used when forming the URI to the image in the registry and
      also the bigtable project name.
  bigtable_instance_name {string}: The name of the instance of Cloud Bigtable
      within the specified project to be used by the Analyzer Service.
  static_ip_address {string}: A static IP address that has already been
     reserved on the GKE cluster.
  """
  image_uri = _image_registry_uri(cloud_project_prefix, cloud_project_name,
                                  ANALYZER_SERVICE_IMAGE_NAME)

  bigtable_project_name = compound_project_name(cloud_project_prefix,
                                                 cloud_project_name)

  context = _form_context_name(cloud_project_prefix, cloud_project_name,
      cluster_zone, cluster_name)

  # If a static ip address is not proviced, then delete the static IP specifier
  # from the descriptor.
  if not static_ip_address:
    static_ip_address = DELETE_LINE_INDICATOR

  # These are the token replacements that must be made inside the deployment
  # template file.
  token_substitutions = {
      '$$ANALYZER_SERVICE_IMAGE_URI$$' : image_uri,
      '$$BIGTABLE_PROJECT_NAME$$' : bigtable_project_name,
      '$$BIGTABLE_INSTANCE_NAME$$' :bigtable_instance_name,
      '$$ANALYZER_PRIVATE_PEM_NAME$$' : ANALYZER_PRIVATE_KEY_PEM_NAME,
      '$$ANALYZER_PRIVATE_KEY_SECRET_NAME$$' : ANALYZER_PRIVATE_KEY_SECRET_NAME,
      '$$ANALYZER_STATIC_IP_ADDRESS$$' : static_ip_address}
  _start_gke_service(ANALYZER_SERVICE_DEPLOYMENT_TEMPLATE_FILE,
                     ANALYZER_SERVICE_DEPLOYMENT_FILE,
                     token_substitutions, context)

def start_report_master(cloud_project_prefix,
                        cloud_project_name,
                        cluster_zone, cluster_name,
                        bigtable_instance_name,
                        static_ip_address):
  """ Starts the report-master deployment and service.
  cloud_project_prefix {sring}: For example "google.com"
  cloud_project_name {sring}: For example "shuffler-test". The prefix and
      name are used when forming the URI to the image in the registry and
      also the bigtable project name.
  bigtable_instance_name {string}: The name of the instance of Cloud Bigtable
      within the specified project to be used by the Report Master.
  static_ip_address {string}: A static IP address that has already been
      reserved on the GKE cluster.
  """
  image_uri = _image_registry_uri(cloud_project_prefix, cloud_project_name,
                                  REPORT_MASTER_IMAGE_NAME)

  bigtable_project_name = compound_project_name(cloud_project_prefix,
                                                 cloud_project_name)

  context = _form_context_name(cloud_project_prefix, cloud_project_name,
      cluster_zone, cluster_name)

  # If a static ip address is not proviced, then delete the static IP specifier
  # from the descriptor.
  if not static_ip_address:
    static_ip_address = DELETE_LINE_INDICATOR

  # These are the token replacements that must be made inside the deployment
  # template file.
  token_substitutions = {
      '$$REPORT_MASTER_IMAGE_URI$$' : image_uri,
      '$$BIGTABLE_PROJECT_NAME$$' : bigtable_project_name,
      '$$BIGTABLE_INSTANCE_NAME$$' :bigtable_instance_name,
      '$$REPORT_MASTER_STATIC_IP_ADDRESS$$' : static_ip_address}
  _start_gke_service(REPORT_MASTER_DEPLOYMENT_TEMPLATE_FILE,
                     REPORT_MASTER_DEPLOYMENT_FILE,
                     token_substitutions, context)

def start_shuffler(cloud_project_prefix,
                   cloud_project_name,
                   cluster_zone, cluster_name,
                   gce_pd_name,
                   static_ip_address,
                   use_memstore=False,
                   danger_danger_delete_all_data_at_startup=False):
  """ Starts the shuffler deployment and service.
  cloud_project_prefix {sring}: For example "google.com"
  cloud_project_name {sring}: For example "shuffler-test". The prefix and
      name are used when forming the URI to the image in the registry.
  gce_pd_name: {string} The name of a GCE persistent disk. This must have
      already been created. The shuffler will use this disk for it LevelDB
      storage so that the data persists between Shuffler updates.
  static_ip_address {string}: A static IP address that has already been
      reserved on the GKE cluster.
  """
  image_uri = _image_registry_uri(cloud_project_prefix, cloud_project_name,
                                  SHUFFLER_IMAGE_NAME)
  # These are the token replacements that must be made inside the deployment
  # template file.
  use_memstore_string = 'false'
  if use_memstore:
    use_memstore_string = 'true'
  delete_all_data = 'false'
  if danger_danger_delete_all_data_at_startup:
    delete_all_data = 'true'

  context = _form_context_name(cloud_project_prefix, cloud_project_name,
      cluster_zone, cluster_name)

  # If a static ip address is not proviced, then delete the static IP specifier
  # from the descriptor.
  if not static_ip_address:
    static_ip_address = DELETE_LINE_INDICATOR

  # These are the token replacements that must be made inside the deployment
  # template file.
  token_substitutions = {'$$SHUFFLER_IMAGE_URI$$' : image_uri,
      '$$GCE_PERSISTENT_DISK_NAME$$' : gce_pd_name,
      '$$SHUFFLER_USE_MEMSTORE$$' : use_memstore_string,
      '$$SHUFFLER_PRIVATE_PEM_NAME$$' : SHUFFLER_PRIVATE_KEY_PEM_NAME,
      '$$SHUFFLER_STATIC_IP_ADDRESS$$' : static_ip_address,
      '$$SHUFFLER_PRIVATE_KEY_SECRET_NAME$$' : SHUFFLER_PRIVATE_KEY_SECRET_NAME,
      '$$DANGER_DANGER_DELETE_ALL_DATA_AT_STARTUP$$' : delete_all_data}
  _start_gke_service(SHUFFLER_DEPLOYMENT_TEMPLATE_FILE,
                     SHUFFLER_DEPLOYMENT_FILE,
                     token_substitutions, context)

def _stop_gke_service(name, context):
  subprocess.check_call(["kubectl", "delete", "service,deployment", name,
                         "--context", context])

def stop_analyzer_service(cloud_project_prefix,
                          cloud_project_name,
                          cluster_zone, cluster_name):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
      cluster_zone, cluster_name)
  _stop_gke_service(ANALYZER_SERVICE_IMAGE_NAME, context)

def stop_report_master(cloud_project_prefix,
                       cloud_project_name,
                       cluster_zone, cluster_name):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
      cluster_zone, cluster_name)
  _stop_gke_service(REPORT_MASTER_IMAGE_NAME, context)

def stop_shuffler(cloud_project_prefix,
                  cloud_project_name,
                  cluster_zone, cluster_name):
  context = _form_context_name(cloud_project_prefix, cloud_project_name,
      cluster_zone, cluster_name)
  _stop_gke_service(SHUFFLER_IMAGE_NAME, context)

def authenticate(cluster_name,
                 cloud_project_prefix,
                 cloud_project_name,
                 cluster_zone):
  cmd = ["gcloud", "container", "clusters", "get-credentials",
      cluster_name, "--project",
      compound_project_name(cloud_project_prefix, cloud_project_name)]
  if cluster_zone:
      cmd.extend(["--zone", cluster_zone])
  subprocess.check_call(cmd)

def display(cloud_project_prefix, cloud_project_name, cluster_zone,
            cluster_name):
   context = _form_context_name(cloud_project_prefix, cloud_project_name,
      cluster_zone, cluster_name)
   subprocess.check_call(["kubectl", "get", "services", "--context", context])

def get_public_uris(cluster_name,
                    cloud_project_prefix,
                    cloud_project_name,
                    cluster_zone):
  """ Returns a dictionary of the public URIs of the deployed services.

  The returned dictionary will have keys "analyzer",
  "report_master", "shuffler". The value for each key will either
  be None or a string representing a URI.

  """

  context = _form_context_name(cloud_project_prefix, cloud_project_name,
      cluster_zone, cluster_name)
  output = subprocess.check_output(["kubectl", "get", "services",
                                    "--context", context])
  # This is an example of what the output of this command might look like.
  # We are going to parse it and extract the relevent data.
  #
  # NAME               CLUSTER-IP       EXTERNAL-IP       PORT(S)          AGE
  # analyzer-service   10.127.253.149   146.148.107.126   6001:30238/TCP   22h
  # kubernetes         10.127.240.1     <none>            443/TCP          6d
  # report-master      10.127.251.229   35.184.143.91     7001:30285/TCP   6d
  # shuffler           10.127.254.94    104.197.122.2     5001:31703/TCP   27m
  #
  values = {}
  dotted_quad = re.compile("^\d+\.\d+\.\d+\.\d+$")
  numeric = re.compile("^\d+$")
  for line in output.split("\n")[1:]:
    columns = line.split()
    if len(columns) != 5:
      continue
    ip_address = columns[2]
    if dotted_quad.match(ip_address) is None:
      continue
    fields = columns[3].split(":")
    if len(fields) < 2:
      continue
    port = fields[0]
    if numeric.match(port) is None:
      continue
    values[columns[0]] = "%s:%s" % (ip_address, port)
  uris = {
    "analyzer" : values.get(ANALYZER_SERVICE_IMAGE_NAME),
    "report_master" : values.get(REPORT_MASTER_IMAGE_NAME),
    "shuffler" : values.get(SHUFFLER_IMAGE_NAME),
  }
  return uris

def main():
  print get_public_uris()

if __name__ == '__main__':
  main()

