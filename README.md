# Cobalt
An extensible, privacy-preserving, user-data analysis pipeline.

[go/cobalt-for-privacy](https://goto.google.com/cobalt-for-privacy)

* Prerequisites
  * Currently this repo has only been tested on Goobuntu.

* One-time setup:
  * This repo uses git modules. After pulling it you must do:
    * `git submodule init`
    * `git submodule update`
  * You must install the following dependencies:
    * clang
    * cmake
    * ninja-build
    * golang
    * protobuf3
    * gRPC
    * run setup.sh as root to install the dependencies

* The script cobaltb.py orchestrates building and testing Cobalt.
  * `cobaltb.py build`
  * `cobaltb.py test`
  * `cobaltb.py -h` for help

* See the *prototype* subdirectory for the Cobalt prototype demo

* Troubleshooting:
  * If setup.sh or compiling fails, try removing any previously installed
    versions of protobuf and grpc.
