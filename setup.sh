#!/bin/bash
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

set -e

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PREFIX="${SCRIPT_DIR}/sysroot"
readonly WD="${PREFIX}/tmp"
readonly GCLOUD_DIR=$PREFIX/gcloud

export PATH=${PREFIX}/bin:${GCLOUD_DIR}/google-cloud-sdk/bin:${PATH}

# scratch dir (if needed)
rm -fr $WD
mkdir -p $WD

function download_prebuilt() {
    local name="${1}"
    local sha="${2}"
    local tarball="${name}.tar.bz2"

    echo Downloading $name

    curl -o $tarball \
        https://storage.googleapis.com/fuchsia-build/fuchsia/$name/linux64/$sha
}

# Install build tools
if [ ! -f $PREFIX/bin/cmake ] ||
   [ "$(cmake --version | awk '/version/ {print $3}')" != "3.6.0" ] ; then
    cd $WD
    download_prebuilt "cmake" "aac4acc2931bcc429f2781748077f017ec7f77e0"
    tar xf cmake.tar.bz2 -C .. --strip-components 1

    download_prebuilt "toolchain" "65755afbfd58811c1a478c1c199a7d146f5e79d1"
    tar xf toolchain.tar.bz2 -C .. --strip-components 1

    download_prebuilt "ninja" "cd193042581c22d1792af0a129581c6c03ee98b5"
    mv ninja.tar.bz2 ../bin/ninja
    chmod +x ../bin/ninja

    download_prebuilt "go" "8fabd15119470eccd936d693e89b66ec4ed15b67"
    mkdir ../golang
    tar xf go.tar.bz2 -C ../golang --strip-components 1
    ln -s ../golang/bin/go ../bin/go
    ln -s ../golang/bin/gofmt ../bin/gofmt
fi

# Install gflags
if [ ! -f $PREFIX/lib/libgflags.a ] ; then
    cd $WD
    mkdir gflags
    cd gflags
    cmake -DCMAKE_INSTALL_PREFIX=$PREFIX $SCRIPT_DIR/third_party/gflags
    make install
fi

# Install protobuf 3
if [ ! -f $PREFIX/bin/protoc ] ||
   [ "$(protoc --version | awk '{print $2}')" != "3.0.0" ] ; then
    cd $WD
    wget https://github.com/google/protobuf/releases/download/v3.0.2/protobuf-cpp-3.0.2.tar.gz
    tar zxf protobuf-cpp-3.0.2.tar.gz
    cd protobuf-3.0.2
    ./configure --prefix=$PREFIX
    make install
fi

# Install gRPC
if [ ! -f $PREFIX/bin/grpc_cpp_plugin ] ; then
    cd $WD
    git clone -b v1.0.0 https://github.com/grpc/grpc
    cd grpc
    git submodule update --init
    make prefix=$PREFIX install
fi

# Install google APIs (C++ protobuf stubs)
if [ ! -f $PREFIX/lib/libgoogleapis.so ] ; then
    cd $WD
    git clone https://github.com/googleapis/googleapis.git
    cd googleapis
    git checkout 2360492797130743eb4c78951fbef6023c1504b8
    make LANGUAGE=cpp GPRCPLUGIN=`which grpc_cpp_plugin`

    echo
    echo Compiling libgoogleapis.  This will take 5 minutes.
    cd gens
    find . -name \*.cc \
      | xargs clang++ -o libgoogleapis.so -fPIC -shared --std=c++11 -I .

    mv libgoogleapis.so $PREFIX/lib

    # copy headers
    cd google
    mkdir -p $PREFIX/include/google
    find . -name \*.h -exec cp --parents {} $PREFIX/include/google/ \;
fi

# Build and install protoc-gen-go binary
GOPATH=${SCRIPT_DIR}/third_party/go
export GOPATH
# XXX Don't know how to check the version so always build it.  The build is
# quick.
echo Building protoc-gen-go
cd $WD
go build -o $PREFIX/bin/protoc-gen-go github.com/golang/protobuf/protoc-gen-go

unset GOPATH

# Install go dependencies
export GOPATH=$PREFIX/go

if [ ! -d $GOPATH/src/cloud.google.com/go/bigtable ] ; then
    echo Installing the bigtable client for go
    mkdir -p $GOPATH
    go get cloud.google.com/go/bigtable
fi

# Install gcloud
if [ ! -f $GCLOUD_DIR/google-cloud-sdk/bin/gcloud ] ; then
    SDK=google-cloud-sdk-131.0.0-linux-x86_64.tar.gz

    cd $WD
    mkdir -p $GCLOUD_DIR
    wget https://dl.google.com/dl/cloudsdk/channels/rapid/downloads/$SDK

    tar -f $SDK -C $GCLOUD_DIR -x
    cd $GCLOUD_DIR
    ./google-cloud-sdk/install.sh --quiet --command-completion false \
                                  --path-update false
    ./google-cloud-sdk/bin/gcloud --quiet components update beta
    ./google-cloud-sdk/bin/gcloud --quiet components install bigtable
fi

rm -fr $WD
