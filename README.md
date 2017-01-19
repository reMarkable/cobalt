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

### Running the Analyzer Service Locally

You can run the Analyzer Service locally using an in-memory data store as follows:

* Let `<root>` be the root of your Cobalt installation.

* `export LD_LIBRARY_PATH=<root>/sysroot/lib`

* `./out/analyzer/analyzer -for_testing_only_use_memstore -port=8080 -cobalt_config_dir="config/registered" -logtostderr`

### Bigtable emulator

You can run the analyzer locally using the Bigtable emulator as follows.  Start
the Bigtable emulator:

* `./sysroot/gcloud/google-cloud-sdk/platform/bigtable-emulator/cbtemulator`

In a separate console run:

* `export LD_LIBRARY_PATH=<root>/sysroot/lib`

* `./out/analyzer/analyzer -for_testing_only_use_bigtable_emulator -port=8080 -cobalt_config_dir="config/registered" -logtostderr`

### Shuffler

 You can run the shuffler locally as follows:

* `/out/shuffler/shuffler -logtostderr -config_file out/config/shuffler_default.conf -batch_size 100 -vmodule=receiver=2,dispatcher=1,store=2`


## cgen: Cobalt gRPC generator

The following example sends a single RPC containing an ObservationBatch with 200
Observations to an Analyzer running locally on port 8080:

* `export LD_LIBRARY_PATH=<root>/sysroot/lib`

* `./out/tools/cgen -analyzer_uri="localhost:8080" -num_observations=200`

The following example sends the RPC instead to the shuffler running locally on port 50051:


* `./out/tools/cgen -analyzer_uri="localhost:8080" -shuffler_uri="localhost:50051" -num_observations=200`

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
