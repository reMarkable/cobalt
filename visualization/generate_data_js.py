#!/usr/bin/python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

""" This script reads several csv files containing data to be visualized.
    It then uses the Google Data Visualization API to generate a JavaScript
    file that contains definitions of DataTables holding the data.
    This is used by visualization.html to generate visualizations.
"""

import csv
import os
import sys

THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
sys.path.insert(0, ROOT_DIR)

import utils.file_util as file_util
import third_party.google_visualization.gviz_api as gviz_api

# The javascript variables to write.
USAGE_BY_MODULE_JS_VAR_NAME = 'usage_by_module_data'
USAGE_BY_CITY_JS_VAR_NAME = 'usage_by_city_data'
USAGE_BY_HOUR_JS_VAR_NAME = 'usage_by_hour_data'

# The outut JavaScript file to be created.
OUTPUT_JS_FILE_NAME = 'data.js'

def buildDataTableJs(data=None, var_name=None, description=None,
    columns_order=None, order_by=()):
  """Builds a JavaScript string defining a DataTable containing the given data.

  Args:
    data: {dictionary}:  The data with which to populate the DataTable.
    var_name {string}: The name of the JavaScript variable to write.
    description {dictionary}: Passed to the constructor of gviz_api.DataTable()
    columns_order {tuple of string}: The names of the table columns in the
      order they should be written. Optional.
    order_by {tuple of string}: Optional. Specify something like ('foo', 'des')
      to sort the rows by the 'foo' column in descending order.

  Returns:
    {string} of the formm <var_name>=<json>, where |var_name| is
    USAGE_BY_HOUR_JS_VAR_NAME and |json| is a json string defining
    a data table.
  """
  # Load data into a gviz_api.DataTable
  data_table = gviz_api.DataTable(description)
  data_table.LoadData(data)
  json = data_table.ToJSon(columns_order=columns_order,order_by=order_by)

  return "%s=%s;" % (var_name, json)

def buildUsageByModuleJs():
  """Reads a CSV file containing the usage-by-module data and uses it
  to build a JavaScript string defining a DataTable containing the data.

  Returns:
    {string} of the form <var_name>=<json>, where |var_name| is
    USAGE_BY_MODULE_JS_VAR_NAME and |json| is a json string defining
    a data table.
  """
  # Read the data from the csv file and put it into a dictionary.
  with file_util.openForReading(
      file_util.USAGE_BY_MODULE_CSV_FILE_NAME) as csvfile:
    reader = csv.reader(csvfile)
    data = [{"module" : row[0], "count": int(row[1])} for row in reader]
  return buildDataTableJs(
      data=data,
      var_name=USAGE_BY_MODULE_JS_VAR_NAME,
      description={"module": ("string", "Module"),
                   "count": ("number", "Count")},
      columns_order=("module", "count"),
      order_by=("count", "desc"))


def buildUsageByCityJs():
  """Reads a CSV file containing the usage-by-city data and uses it
  to build a JavaScript string defining a DataTable containing the data.

  Returns:
    {string} of the form <var_name>=<json>, where |var_name| is
    USAGE_BY_CITY_JS_VAR_NAME and |json| is a json string defining
    a data table.
  """
  # Read the data from the csv file and put it into a dictionary.
  with file_util.openForReading(
      file_util.USAGE_BY_CITY_CSV_FILE_NAME) as csvfile:
    reader = csv.reader(csvfile)
    data = [{"city" : row[0], "usage": int(row[1]), "rating": float(row[2])}
        for row in reader]
  # We write the rating column before the usage colun because this will be used
  # in a geo chart and the first column determines the color and the second
  # colun determines the size of the circles.
  return buildDataTableJs(
      data=data,
      var_name=USAGE_BY_CITY_JS_VAR_NAME,
      description = {"city": ("string", "City"),
                     "rating": ("number", "Rating"),
                     "usage": ("number", "Usage")},
      columns_order=("city", "rating", "usage"),
      order_by=("usage", "desc"))

def buildUsageByHourJs():
  """Reads a CSV file containing the usage-by-hour data and uses it
  to build a JavaScript string defining a DataTable containing the data.

  Returns:
    {string} of the form <var_name>=<json>, where |var_name| is
    USAGE_BY_HOUR_JS_VAR_NAME and |json| is a json string defining
    a data table.
  """
  # Read the data from the csv file and put it into a dictionary.
  with file_util.openForReading(
      file_util.USAGE_BY_HOUR_CSV_FILE_NAME) as csvfile:
    reader = csv.reader(csvfile)
    # Read up to 10,000 rows adding the row index as "hour".
    data = [{"hour" : i, "usage": int(row[0])}
        for (i, row) in zip(xrange(10000), reader)]
  return buildDataTableJs(
      data=data,
      var_name=USAGE_BY_HOUR_JS_VAR_NAME,
      description = {"hour": ("number", "Hour of Day"),
                     "usage": ("number", "Usage")},
      columns_order=("hour", "usage"))

def main():
  # Read the input file and build the JavaScript strings to write.
  usage_by_module_js = buildUsageByModuleJs()
  usage_by_city_js = buildUsageByCityJs()
  usage_by_hour_js = buildUsageByHourJs()

  # Write the output file.
  with file_util.openForWriting(OUTPUT_JS_FILE_NAME) as f:
    f.write("// This js file is generated by the script "
            "generate_data_js.py\n\n")
    f.write("%s\n\n" % usage_by_module_js)
    f.write("%s\n\n" % usage_by_city_js)
    f.write("%s\n\n" % usage_by_hour_js)

if __name__ == '__main__':
  main()