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
    versions of protobuf, grpc and golang.

## Google Container Engine (GCE)

The cobaltb.py tool is also a helper to interact with GCE.  The following
commands are supported:

gce\_build   - Build the docker images for use on GCE.

gce\_push    - Publish the built docker images to the GCE repository.

gce\_start   - Start the cobalt components.  To see the external IP of services
               run, for example: kubectl get service analyzer

gce\_stop    - Stops the cobalt components.

### GCE Prerequisites

* sudo apt-get install docker-engine

* sudo usermod -aG docker
  * You'll have to login again to be added to the docker group.

* Install gcloud: https://cloud.google.com/sdk/

* gcloud init

* gcloud components install kubectl

### GCE Authentication

Tasks like gce\_start, gce\_stop or running the Analyzer outside of GCE require
setting the environment variable GOOGLE\_APPLICATION\_CREDENTIALS to point to
the JSON file containing credentials.  Follow the instructions of step "1." of
"How the Application Default Credentials work" from:

<https://developers.google.com/identity/protocols/application-default-credentials>
