# sysroot

Cobalt's build tools (CMake, toolchain, ninja, go and protoc-gen-go) and some
other developer tools (bigtable tool and cloud SDK) are downloaded pre-builts
installed into the sysroot directory using the `setup.sh` script. Normally
this script should be invoked via the command `cobaltb.py setup`. This
causes a single `sysroot.tgz` file to be downloaded from Google Cloud Storage.
The sha1 hash of the downloaded file is verified.

To build and upload a new version of sysroot to Google storage do the
following:

* `rm -fr sysroot`
* `./setup.sh -u`
* Edit setup.sh and modify VERSION to have the SHA of the new sysroot.
