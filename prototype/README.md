# Cobalt Prototype Demo

* WARNING: Do not deploy this code to production---it is not secure! This
implementation is intended only as a prototype. In particular the PyCrypto
library has not been approved by the Google ISE team.

* Prerequisites
  * Currently this demo is only supported on Ubuntu Linux.

* One-time setup. You must install R and some other packages:
  * `cd third_party/rappor`
  * `./setup.sh`
  * You will be asked to enter your sudo password.
  * The setup script may take a few minutes to install everything.
  * You may see a few error messages but if the script keeps going everything
    is probably ok.
  * `cd ../..`

* More one-time setup. You must install the Python cryptography package.
  * `sudo apt-get install build-essential libssl-dev libffi-dev python-dev`
  * `sudo pip install cryptography`

* ` ./cobalt.py build`
  * This generates a fastrand python module that wraps a fast C random
    number generator and a fast_em module that is invoked from RAPPOR R
    code to perform a fast expectation maximization iteration.

* ` ./cobalt.py test`
  * Runs all tests.

* `./cobalt.py run`
  * This generates synthetic data, runs the straight-counting pipeline, runs the
  Cobalt prototype pipeline, and generates a visualization of the results. You
  can also *run individual steps manually* as follows:

  * `./cobalt.py clean`
    * This deletes the |out| directory.

  * `./cobalt.py gen`

    * This creates an `out` directory and generates synthetic data in the file
  `input_data.csv` in the `out` directory. This data is the input to both the
  straight counting pipeline and the Cobalt prototype pipeline.

    * This script also runs the straight counting pipeline that emits several
  output files into the `out` directory:
      * `popular_help_queries.csv`
      * `usage_and_rating_by_city.csv`
      * `usage_by_hour.csv`
      * `usage_by_module.csv`

  * `./cobalt.py randomize`
    * This reads `input_data.csv` and runs all randomizers on that data. This
    constitutes the first stage of the Cobalt prototype pipeline. A randomizer
    emits its data to a csv file in the `r_to_s` subdirectory below `out`.

  * `./cobalt.py shuffle`
    * This runs all shufflers. A shuffler reads randomizer output from
    the `r_to_s` directory and writes data in the `s_to_a` directory.

  * `./cobalt.py analyze`
    * This runs all analyzers. An analyzer reads shuffler output from
    the `s_to_a` directory and writes final output into the `out` directory.

  * `./cobalt.py visualize`
    * This reads the files output by the straight counting pipeline and the
      Cobalt prototype pipeline and generates `data.js` using the Google
      visualization API.

* Load `./visualization/visualization.html` in your browser
  * This displays some charts based on the data in `data.js`
