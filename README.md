# Cobalt
An extensible, privacy-preserving, user-data analysis pipeline.

[go/cobalt-for-privacy](https://goto.google.com/cobalt-for-privacy)

* Prerequisites
  * Currently this repo has only been tested on Goobuntu.

* One-time setup: This repo uses git modules. After pulling it you must do:
  * `git submodule init`
  * `git submodule update`

* The script cobaltb.py orchestrates building and testing Cobalt.
  * `python cobaltb.py build`
  * `python cobaltb.py test`
  * `python cobaltb.py -h` for help

* See the *prototype* subdirectory for the Cobalt prototype demo
