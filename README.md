# Cobalt Prototype Demo

* `./cobalt.py run`
  * This generates synthetic data, runs the straight-counting pipeline, runs the
  Cobalt prototype pipeline, and generates a visualization of the results. You
  can also *run individual steps manually* as follows:

  * `./fake_data/generate_fake_data.py`

    * This creates an `out` directory and generates synthetic data in the file
  `input_data.csv` in the `out` directory. This data is the input to both the
  straight counting pipleine and the Cobalt prototype pipeline.

    * This script also runs the straight counting pipeline that emits several
  output files into the `out` directory:
      * `popular_help_queries.csv`
      * `usage_and_rating_by_city.csv`
      * `usage_by_hour.csv`
      * `usage_by_module.csv`

  * `./randomizers/randomizer.py`
    * This reads `input_data.csv` and runs all randomizers on that data. This
    constitutes the first stage of the Cobalt prototype pipeline. A randomizer
    emits its data to a csv file in the `r_to_s` subdirectory below `out`.

  * `./shufflers/shuffler.py`
    * This runs all shufflers. A shuffler reads randomizer output from
    the `r_to_s` directory and writes data in the `s_to_a` directory.

  * `./analyzers/analyzer.py`
    * This runs all analyzers. An analyzer reads shuffler output from
    the `s_to_a` directory and writes final output into the `out` directory.

  * `./visualization/generate_data_js.py`
    * This reads the files output by the straight counting pipeline and the
      Cobalt prototype pipeline and generates `data.js` using the Google
      visualization API.

* Load `./visualization/visualization.html` in your browser
  * This displays some charts based on the data in `data.js