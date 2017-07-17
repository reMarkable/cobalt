#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Uploads report_client to Google Cloud Storage.

We use the script upload_to_google_storage.py from depot_tools. You must
have this in your path.

We tar and gzip compress the binary report_client forming a .tgz file. We then
upload this .tgz file to the Google Cloud Storage bucket
fuchsia-build/cobalt/report_client/<platform>
using the sha1 of the .tgz file as the filename.

Some typical values of <platform> are linux64 and darwin64.

"""

import os
import platform
import shutil
import subprocess
import sys
import tarfile

THIS_DIR = os.path.dirname(__file__)
SRC_ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.pardir))
OUT_DIR = os.path.join(SRC_ROOT_DIR, 'out')
TOOLS_GO_DIR = os.path.join(SRC_ROOT_DIR, 'tools', 'go')

def _write_compressed_tarfile(source_files, tarfile_name):
  with tarfile.open(tarfile_name, "w:gz") as tar:
    for file in source_files:
      tar.add(file)

def _platform_string():
  return '%s%s' % (platform.system().lower(), platform.architecture()[0][:2])

def _upload(file_to_upload, platform_string):
  bucket_name = 'fuchsia-build/cobalt/report_client/%s' % platform_string
  cmd = ['upload_to_google_storage.py', '-b', bucket_name, file_to_upload]
  subprocess.check_call(cmd)

def main():
  report_client_path = os.path.join(OUT_DIR, 'tools', 'report_client')
  if not os.path.exists(report_client_path):
    print "File not found: %s." % report_client_path
    print "You must build first."
    return 1
  platform_string = _platform_string()
  temp_tgz_file = 'report_client.%s.tgz' % platform_string
  print "Compressing %s to temporary file %s..." % (report_client_path,
      temp_tgz_file)
  _write_compressed_tarfile([report_client_path], temp_tgz_file)
  _upload(temp_tgz_file, platform_string)
  os.remove(temp_tgz_file)
  temp_sha1_file = "%s.sha1" % temp_tgz_file
  final_sha1_file = os.path.join(TOOLS_GO_DIR, temp_sha1_file)
  shutil.move(temp_sha1_file, final_sha1_file)

if __name__ == '__main__':
  main()
