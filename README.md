# Cobalt Prototype Demo

These are the current instructions for running the straight counting pipeline
and the Cobalt prototype pipeline. The instructions are evolving along with the
code.

* Run `./fake_data/generate_fake_data.py`

  * This creates an `out` directory and generates synthetic data in the file
  `input_data.csv` in the `out` directory. This data is the input to both the
  straight counting pipleine and the Cobalt prototype pipeline.

  * This script also runs the straight counting pipeline that emits several
  output files into the `out` directory:
    * `popular_help_queries.csv`
    * `usage_and_rating_by_city.csv`
    * `usage_by_hour.csv`
    * `usage_by_module.csv`

* Run `./randomizers/randomizer.py`
  * This reads `input_data.csv` and runs all randomizers on that data. This
  constitutes the first stage of the Cobalt prototype pipeline. A randomizer
  emits its data to a csv file in the `r_to_a` subdirectory below `out`.

* Run `./visualization/generate_data_js.py`
  * This reads the files output by the straight counting pipeline and the
  Cobalt prototype pipeline and generates `data.js` using the Google
  visualization API.

* Load `./visualization/visualization.html` in your browser
  * This displays some charts based on the data in `data.js`
