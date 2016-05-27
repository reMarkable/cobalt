# Generate fake data and visualize it

- Run `./fake_data/generate_fake_data.py`
- This creates an `out` directory and creates three csv files of fake data in
  it.
- Run `./visualization/generate_data_js.py`
- This reads the three csv files and generates `data.js` using the Google
  visualization api.
- Load `./visualization/visualization.html` in your browser
- This displays three charts based on the data in `data.js`