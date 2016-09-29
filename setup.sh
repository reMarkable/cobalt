#!/bin/sh
#
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
#
# Installs cobalt dependencies.
# Warning this will install packages globally

PREFIX=/usr

if [ $(id -u) != "0" ] ; then
    echo run as root
    exit 1
fi

echo "This will install cobalt dependencies globally ($PREFIX)"
echo "Enter y to proceed"

read Y

if [ "$Y" != 'y' ] ; then
    echo Aborting...
    exit 1
fi

# scratch dir (if needed)
WD=/tmp/cobalt_setup
rm -fr $WD
mkdir $WD

# Install apt packages
apt-get -y install clang cmake ninja-build golang

# Install protobuf 3
if ! which protoc > /dev/null ||
   [ "$(protoc --version | awk '{print $2}')" != "3.0.0" ] ; then
    cd $WD
    wget https://github.com/google/protobuf/releases/download/v3.0.2/protobuf-cpp-3.0.2.tar.gz
    tar zxf protobuf-cpp-3.0.2.tar.gz
    cd protobuf-3.0.2
    ./configure --prefix=$PREFIX
    make install
fi

# Install gRPC
if ! which grpc_cpp_plugin > /dev/null ; then
    cd $WD
    git clone -b v1.0.0 https://github.com/grpc/grpc
    cd grpc
    git submodule update --init
    make prefix=$PREFIX install
fi

rm -fr $WD
