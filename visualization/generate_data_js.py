#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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

# The javascript variables to write. Note "_sc" refers to the data from
# the "straight-counting pipeline"
USAGE_BY_MODULE_JS_VAR_NAME = 'usage_by_module_data'
USAGE_BY_MODULE_SC_JS_VAR_NAME = 'usage_by_module_data_sc'

USAGE_BY_CITY_SC_JS_VAR_NAME = 'usage_by_city_data_sc'
USAGE_BY_HOUR_SC_JS_VAR_NAME = 'usage_by_hour_data_sc'
POPULAR_HELP_QUERIES_SC_JS_VAR_NAME = 'popular_help_queries_data_sc'

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
    {string} of the form |var_name|=<json>, where <json> is a json string
    defining a data table.
  """
  # Load data into a gviz_api.DataTable
  data_table = gviz_api.DataTable(description)
  data_table.LoadData(data)
  json = data_table.ToJSon(columns_order=columns_order,order_by=order_by)

  return "%s=%s;" % (var_name, json)

def buildUsageByModuleJs():
  """Reads two CSV files containing the usage-by-module data for the straight-
  counting pipeline and the Cobalt prototype pipeline respectively and uses them
  to build two JavaScript strings defining DataTables containing the data.

  Returns:
    {tuple of two strings} (sc_string, cobalt_string). Each of the two strings
    is of the form <var_name>=<json>, where |json| is a json string defining
    a data table. The |var_name|s are respectively
    USAGE_BY_MODULE_SC_JS_VAR_NAME and USAGE_BY_MODULE_JS_VAR_NAME.
  """
  # straight-counting:
  # Read the data from the csv file and put it into a dictionary.
  with file_util.openForReading(
      file_util.USAGE_BY_MODULE_CSV_FILE_NAME) as csvfile:
    reader = csv.reader(csvfile)
    # |data| will be used to generate the visualiation data for the
    # straight-counting pipeline
    data = []
    # |values| will be used below to include the actual values along with
    # the RAPPOR estimates in the visualization of the Cobalt pipeline.
    values = {}
    for row in reader:
      data.append({"module" : row[0], "count": int(row[1])})
      values[row[0]] = int(row[1])
  usage_by_module_sc_js = buildDataTableJs(
      data=data,
      var_name=USAGE_BY_MODULE_SC_JS_VAR_NAME,
      description={"module": ("string", "Module"),
                   "count": ("number", "Count")},
      columns_order=("module", "count"),
      order_by=("count", "desc"))

  # cobalt:
  # Here the CSV file is the output of the RAPPOR analyzer.
  # We read it and put the data into a dictionary.
  # We skip row zero because it is the header row. We are going to visualize
  # the data as an interval chart and so we want to compute the high and
  # low 95% confidence interval values wich we may do using the "std_error"
  # column, column 2.
  with file_util.openForReading(
      file_util.MODULE_NAME_ANALYZER_OUTPUT_FILE_NAME) as csvfile:
    reader = csv.reader(csvfile)
    data = [{"module" : row[0], "estimate": float(row[1]),
             "actual" : values.get(row[0], 0),
             "low" : float(row[1]) - 1.96 * float(row[2]),
             "high": float(row[1]) + 1.96 * float(row[2])}
        for row in reader if reader.line_num > 1]
  usage_by_module_cobalt_js = buildDataTableJs(
      data=data,
      var_name=USAGE_BY_MODULE_JS_VAR_NAME,
      description={"module": ("string", "Module"),
                   "estimate": ("number", "Estimate"),
                   "actual": ("number", "Actual"),
                   # The role: 'interval' property is what tells the Google
                   # Visualization API to draw an interval chart.
                   "low": ("number", "Low", {'role':'interval'}),
                   "high": ("number", "High", {'role':'interval'})},
      columns_order=("module", "estimate", "actual", "low", "high"),
      order_by=("estimate", "desc"))

  return (usage_by_module_sc_js, usage_by_module_cobalt_js)


def buildUsageByCityJs():
  """Reads a CSV file containing the usage-by-city data and uses it
  to build a JavaScript string defining a DataTable containing the data.

  Returns:
    {string} of the form <var_name>=<json>, where |var_name| is
    USAGE_BY_CITY_SC_JS_VAR_NAME and |json| is a json string defining
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
      var_name=USAGE_BY_CITY_SC_JS_VAR_NAME,
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
    USAGE_BY_HOUR_SC_JS_VAR_NAME and |json| is a json string defining
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
      var_name=USAGE_BY_HOUR_SC_JS_VAR_NAME,
      description = {"hour": ("number", "Hour of Day"),
                     "usage": ("number", "Usage")},
      columns_order=("hour", "usage"))

def buildPopularHelpQueriesJs():
  """Reads a CSV file containing the popular help queries data and uses it
  to build a JavaScript string defining a DataTable containing the data.

  Returns:
    {string} of the form <var_name>=<json>, where |var_name| is
    POPULAR_HELP_QUERIES_SC_JS_VAR_NAME and |json| is a json string defining
    a data table.
  """
  # Read the data from the csv file and put it into a dictionary.
  with file_util.openForReading(
      file_util.POPULAR_HELP_QUERIES_CSV_FILE_NAME) as csvfile:
    reader = csv.reader(csvfile)
    data = [{"help_query" : row[0], "count": int(row[1])} for row in reader]
  return buildDataTableJs(
      data=data,
      var_name=POPULAR_HELP_QUERIES_SC_JS_VAR_NAME,
      description={"help_query": ("string", "Help queries"),
                   "count": ("number", "Count")},
      columns_order=("help_query", "count"),
      order_by=("count", "desc"))

def main():
  print "Generating visualization..."

  # Read the input file and build the JavaScript strings to write.
  usage_by_module_sc_js, usage_by_module_js = buildUsageByModuleJs()
  usage_by_city_js = buildUsageByCityJs()
  usage_by_hour_js = buildUsageByHourJs()
  popular_help_queries_js = buildPopularHelpQueriesJs()

  # Write the output file.
  with file_util.openForWriting(OUTPUT_JS_FILE_NAME) as f:
    f.write("// This js file is generated by the script "
            "generate_data_js.py\n\n")
    f.write("%s\n\n" % usage_by_module_sc_js)
    f.write("%s\n\n" % usage_by_module_js)

    f.write("%s\n\n" % usage_by_city_js)
    f.write("%s\n\n" % usage_by_hour_js)
    f.write("%s\n\n" % popular_help_queries_js)

  print "View this file in your browser:"
  print "file://%s" % file_util.VISUALIZATION_FILE

if __name__ == '__main__':
  main()
