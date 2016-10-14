Docker images
=============

cobalt/     - The base cobalt image.

analyzer/   - Imports the base cobalt image and adds the analyzer binaries.

shuffler/   - Imports the base cobalt image and adds the shuffler binaries.

To build the docker images run: ../cobaltb.py gce\_build
