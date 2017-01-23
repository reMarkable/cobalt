# Cobalt
An extensible, privacy-preserving, user-data analysis pipeline.
[go/cobalt-for-privacy](https://goto.google.com/cobalt-for-privacy)

* Prerequisites
  * Currently this repo has only been tested on Ubuntu.

* One-time setup:
    `cobaltb.py setup`
  * This will setup the git submodules and will install dependencies in the sysroot directory.
  See the [sysroot](#sysroot) section below for details.

* The script cobaltb.py orchestrates building and testing Cobalt.
  * `cobaltb.py build`
  * `cobaltb.py test`
  * `cobaltb.py lint` to run the linters
  * `cobaltb.py -h` for general help

## Running the tests
It is possible to run various subsets of the tests. Run `cobaltb.py test -h` to see the possible subsets.

#### Memory-Store tests
`./cobaltb.py test --tests=gtests`
This runs all of the gunit tests. For those tests that need a DataStore, the in-memory datastore is used.
Compare this against the other possibilities for a DataStore listed below.

#### Bigtable Emulator tests
`./cobaltb.py test --tests=btemulator`
This starts the Bigtable emulator running on the local machine on the default port of 9000. Then it runs various
gunit tests that need a DataStore and that expect to find the emulator running. These tests use the BigtableStore as
the DataStore and BigtableStore connects to the Bigtable emulator.

#### Cloud Bigtable tests
These are a set of gunit tests that use BigtableStore as the DataStore and connect to the real Google
Cloud Bigtable. These tests are not run automatically at this time. They are not run on the build machine and they are *not*
run if you type `./cobaltb.py test --tests=all`. Instead you must explictly invoke them. See below for the command line.

**One-time setup:** You must install a credential on your computer in order for the Cobalt code running
on your computer to be able to access Cobalt's testing Google Cloud Project.
* Go to the [Credentials page](https://pantheon.corp.google.com/apis/credentials?debugUI=DEVELOPERS&project=google.com:shuffler-test)
of the project.
* Click `Create Credentials`
* Select `Service Account Key` as the type of key
* In the Service Account dropdown select `New Service Account` and assign your service account any name.
* Select `JSON` as the key type
* Click `Create`
* A JSON file containing the credentials will be generated and downloaded to your computer. Save it anywhere.
* Rename and move the JSON file. You must name the file exactly `service_account_credentials.json` and you must put
the file in the Cobalt source root directory (next to this README file.)
* cobaltb.py sets the environment variable `GOOGLE_APPLICATION_CREDENTIALS` to the path to that file. This is
necessary for the gRPC code linked with Cobalt to find the credential at run-time during the tests.

*Side-note:* We are abusing service accounts with this procedure. The more appropriate solution is to use oauth
tokens in order to authenticate your computer to Google Cloud. However at this time there seems to be a [bug
that is preventing this from working](https://github.com/grpc/grpc/issues/7131). The symptom is you will see
the following error message: ` assertion failed: deadline.clock_type == g_clock_type`. If you see this error
message it means that the oauth flow is being attempted and has hit this bug. This happens if the gRPC code is
not able to use the service account credential located at `GOOGLE_APPLICATION_CREDENTIALS`.

If you want, it is possible to use a different Google Cloud project besides Cobalt's testing project.
Follow all the same steps above using any project. Then later you will have to pass the command-line
flag `--bigtable_project_name` with the name of your project.

**Every-time setup** You will also need to create your own personal instance of Cloud Bigtable within
the Google Cloud project for which you created credentials above. Because it costs our cost center
money to have have an instance of Google Cloud Bigtable running, we recommend not leaving your personal
instance running all the time. We recommend creating an instance when you want to test with it and
then deleting it when you are done.

* Go to the [Bigtable instance creation page](https://pantheon.corp.google.com/bigtable/instances?project=google.com:shuffler-test)
of Cobalt's testing Google Cloud project, or the project for which you created credentials above.
* Select `CREATE INSTANCE`
* Name your instance whatever you would like
* Important: Please select **Development** and not *Production*. This is one-third the cost.
* Now run `./cobaltb.py test --tests=cloud_bt --bigtable_instance_name=<your-personal-instance>`
* You can also pass the flag `--bigtable_project_name=<your-project-name>` if you want to use a different project.
* If you don't plan to use your instance for a few days please delete it and then recreate another one later.


## Running the Analyzer Service Locally

You can run the Analyzer Service locally using an in-memory data store as follows:

* Let `<root>` be the root of your Cobalt installation.

* `export LD_LIBRARY_PATH=<root>/sysroot/lib`

* `./out/analyzer/analyzer -for_testing_only_use_memstore -port=8080 -cobalt_config_dir="config/registered" -logtostderr`

## Bigtable emulator

You can run the analyzer locally using the Bigtable emulator as follows.  Start
the Bigtable emulator:

* `./sysroot/gcloud/google-cloud-sdk/platform/bigtable-emulator/cbtemulator`

In a separate console run:

* `export LD_LIBRARY_PATH=<root>/sysroot/lib`

* `./out/analyzer/analyzer -for_testing_only_use_bigtable_emulator -port=8080 -cobalt_config_dir="config/registered" -logtostderr`

## Shuffler

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

## GCE Prerequisites

* sudo apt-get install docker-engine

* sudo usermod -aG docker
  * You'll have to login again to be added to the docker group.

* Install gcloud: https://cloud.google.com/sdk/

* gcloud init

* gcloud components install kubectl

## GCE Authentication

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
