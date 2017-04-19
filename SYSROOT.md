# sysroot

All of Cobalt's dependencies (both compile and run-time) are installed in the
sysroot directory using the `setup.sh` script.  To avoid having to compile the
dependencies every time, a packaged binary version of sysroot is stored on
Google storage.  `cobaltb.py setup` will download the pre-built sysroot from
Google storage.

To upload a new version of sysroot on Google storage do the following:

* `rm -fr sysroot`
* `./setup.sh -u`
* Edit setup.sh and modify VERSION to have the SHA of the new sysroot.
