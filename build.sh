#!/bin/bash
#
# Build automation.
#
# Usage:
#   ./build.sh fastrand
#
# Important targets are:
#   fastrand: build Python extension module to speed up the client simulation


set -o nounset
set -o pipefail
set -o errexit

log() {
  echo 1>&2 "$@"
}


# Build dependencies: Python development headers.  Most systems should have
# this.  On Ubuntu/Debian, the 'python-dev' package contains headers.
fastrand() {
  pushd fastrand  >/dev/null
  python setup.py build
  # So we can 'import _fastrand' without installing
  ln -s --force build/*/_fastrand.so .
  ./fastrand_test.py

  log 'fastrand built and tests PASSED'
  popd >/dev/null
}

if test $# -eq 0 ; then
  fastrand
else
  "$@"
fi
