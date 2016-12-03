# Cobalt
An extensible, privacy-preserving, user-data analysis pipeline.

[go/cobalt-for-privacy](https://goto.google.com/cobalt-for-privacy)

* Prerequisites
  * Currently this repo has only been tested on Goobuntu.

* One-time setup:
  * `cobaltb.py setup`
  * It will setup the git submodules and will install the following
    dependencies:
    * clang
    * cmake
    * golang
    * glog
    * gRPC
    * ninja-build
    * protobuf3

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

### Bigtable emulator

You can run the analyzer locally using the Bigtable emulator as follows.  Start
the Bigtable emulator:

* gcloud beta emulators bigtable start

Then start the Analyzer with the appropriate environment set:

* $(gcloud beta emulators bigtable env-init)

* analyzer -table projects/google.com:shuffler-test/instances/cobalt-analyzer/tables/observations

Note, you can run the analyzer using the -memstore option to use an internal
in-memory database that doesn't even require Bigtable or its emulator.

## cgen: Cobalt gRPC generator

The following example sends a single RPC containing an ObservationBatch with two
Observations to an Analyzer running locally:

* cgen -analyzer 127.0.0.1 -num\_observations 2

## sysroot (Cobalt's dependencies)

All of Cobalt's dependencies (both compile and run-time) are installed in the
sysroot directory using the `setup.sh` script.  To avoid having to compile the
dependencies every time, a packaged binary version of sysroot is stored on
Google storage.  `cobaltb.py setup` will download the pre-built sysroot from
Google storage.

To upload a new version of sysroot on Google storage do the following:

* `rm -fr sysroot`
* `./setup.sh -u`
* Edit setup.sh and modify VERSION to have the SHA of the new sysroot.
