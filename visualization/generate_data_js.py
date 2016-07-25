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

from randomizers.randomizer import readRapporConfigParamsFromFile

# The javascript variables to write. Note "_sc" refers to the data from
# the "straight-counting pipeline"
USAGE_BY_MODULE_JS_VAR_NAME = 'usage_by_module_data'
USAGE_BY_MODULE_SC_JS_VAR_NAME = 'usage_by_module_data_sc'
USAGE_BY_MODULE_PARAMS_JS_VAR_NAME = 'usage_by_module_params'

USAGE_BY_CITY_SC_JS_VAR_NAME = 'usage_by_city_data_sc'

USAGE_BY_HOUR_SC_JS_VAR_NAME = 'usage_by_hour_data_sc'
USAGE_BY_HOUR_JS_VAR_NAME = 'usage_by_hour_data'
USAGE_BY_HOUR_PARAMS_JS_VAR_NAME = 'usage_by_hour_params'

POPULAR_URLS_JS_VAR_NAME = 'popular_urls_data'
POPULAR_URLS_SC_JS_VAR_NAME = 'popular_urls_data_sc'

POPULAR_HELP_QUERIES_JS_VAR_NAME = 'popular_help_queries_data'
POPULAR_HELP_QUERIES_SC_JS_VAR_NAME = 'popular_help_queries_data_sc'
POPULAR_HELP_QUERIES_HISTOGRAM_SC_JS_VAR_NAME = \
    'popular_help_queries_histogram_data_sc'

# The output JavaScript file to be created.
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
    {tuple of three strings} (sc_string, cobalt_string, rappor_parameters). The
    first two strings are of the form <var_name>=<json>, where |json| is a json
    string defining a data table. The |var_name|s are respectively
    USAGE_BY_MODULE_SC_JS_VAR_NAME and USAGE_BY_MODULE_JS_VAR_NAME.
    rappor_parameters is a json string containing values for k, h, m, p, q, f.
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
  # low 95% confidence interval values which we may do using the "std_error"
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
                   "low": ("number", "Low", {'role': 'interval'}),
                   "high": ("number", "High", {'role': 'interval'})},
      columns_order=("module", "estimate", "actual", "low", "high"),
      order_by=("estimate", "desc"))

  # RAPPOR parameters
  rappor_params_js = "{} = {};".format(
      USAGE_BY_MODULE_PARAMS_JS_VAR_NAME, readRapporConfigParamsFromFile(
          file_util.RAPPOR_MODULE_NAME_CONFIG).to_json())

  return (usage_by_module_sc_js, usage_by_module_cobalt_js, rappor_params_js)


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
  """Reads two CSV files containing the usage-by-hour data for the
  straight-counting pipeline and the Cobalt prototype pipeline respectively and
  uses them to build two JavaScript strings defining DataTables containing the
  data and one string describing basic RAPPOR parameters.

  Returns:
    {tuple of two strings} (sc_string, cobalt_string, params_string). The first
    two strings are of the form <var_name>=<json>, where |json| is a json string
    defining a data table. The |var_name|s are respectively
    USAGE_BY_HOUR_SC_JS_VAR_NAME and USAGE_BY_HOUR_JS_VAR_NAME.
    params_string is a json string containing RAPPOR parameters.
  """
  # straight-counting:
  # Read the data from the csv file and put it into a dictionary.
  with file_util.openForReading(
      file_util.USAGE_BY_HOUR_CSV_FILE_NAME) as csvfile:
    reader = csv.reader(csvfile)
    # |data| will be used to generate the visualiation data for the
    # straight-counting pipeline
    data = []
    # |values| will be used below to include the actual values along with
    # the RAPPOR estimates in the visualization of the Cobalt pipeline.
    values = []
    hour = 0
    for row in reader:
      data.append({"hour" : hour, "usage": int(row[0])})
      values.append(int(row[0]))
      hour += 1
  usage_by_hour_sc_js = buildDataTableJs(
      data=data,
      var_name=USAGE_BY_HOUR_SC_JS_VAR_NAME,
      description = {"hour": ("number", "Hour of Day"),
                     "usage": ("number", "Usage")},
      columns_order=("hour", "usage"))

  # cobalt:
  # Here the CSV file is the output of the RAPPOR analyzer.
  # We read it and put the data into a dictionary.
  # We skip row zero because it is the header row. We are going to visualize
  # the data as an interval chart and so we want to compute the high and
  # low 95% confidence interval values wich we may do using the "std_error"
  # column, column 2.
  with file_util.openForReading(
      file_util.HOUR_ANALYZER_OUTPUT_FILE_NAME) as csvfile:
    reader = csv.reader(csvfile)
    data = [{"hour" : int(row[0]), "estimate": max(float(row[1]), 0),
             "actual": values[int(row[0])],
             "low" : max(float(row[1])  - 1.96 * float(row[2]), 0),
             "high": float(row[1]) + 1.96 * float(row[2])}
        for row in reader if reader.line_num > 1]
  usage_by_hour_cobalt_js = buildDataTableJs(
      data=data,
      var_name=USAGE_BY_HOUR_JS_VAR_NAME,
      description={"hour": ("number", "Hour"),
                   "estimate": ("number", "Estimate"),
                   "actual": ("number", "Actual"),
                   # The role: 'interval' property is what tells the Google
                   # Visualization API to draw an interval chart.
                   "low": ("number", "Low", {'role': 'interval'}),
                   "high": ("number", "High", {'role': 'interval'})},
      columns_order=("hour", "estimate", "low", "high", "actual"),
      order_by=("hour", "asc"))

  # RAPPOR parameters
  rappor_params_js = "{} = {};".format(
      USAGE_BY_HOUR_PARAMS_JS_VAR_NAME,
      readRapporConfigParamsFromFile(file_util.RAPPOR_HOUR_CONFIG).to_json())

  return (usage_by_hour_sc_js, usage_by_hour_cobalt_js, rappor_params_js)

def buildItemAndCountJs(filename, varname1, varname2, item_column,
                        item_description):
  """Reads a CSV file containing two columns, an item column and a
  count column, and and uses the data to build two JavaScript strings defining
  DataTables containing the data. The two DataTables will be the same except
  for the order of the columns: The first DataTable will have the count
  column first and the second DataTable will have the item column first.

  Args:
    filename: {string} The full path of the CSV file to read.
    varname1: {string} The name of the first javascript variable to generate.
    varname2: {string} The name of the second javascript variable to generate.
                       If this is None then the second returned string will
                       also be None.
    item_column: {string} The name of the item column to use in the generated
                          JS.
    item_description: The description string to use in the generated JS.

  Returns:
    {tuple of two string} of the form <varname>=<json>, where <json> is a json
    string defining a data table. In the first returned string <varname> will
    be |varname1| and the "count" column will come first in the DataTable.
    In the second returned string <varname> will be |varname1| and |item_column|
    will come first in the DataTable.
  """

  with file_util.openForReading(filename) as csvfile:
    reader = csv.reader(csvfile)
    data = [{item_column : row[0], "count": int(row[1])} for row in reader]
  count_first_string = buildDataTableJs(
      data=data,
      var_name=varname1,
      description={item_column: ("string", item_description),
                   "count": ("number", "Count")},
      columns_order=("count", item_column),
      order_by=(("count", "desc"), item_column))
  item_first_string = None
  if varname2 is not None:
    item_first_string = buildDataTableJs(
        data=data,
        var_name=varname2,
        description={item_column: ("string", item_description),
                    "count": ("number", "Count")},
        columns_order=(item_column, "count"),
        order_by=(("count", "desc"), item_column))
  return (count_first_string, item_first_string)

def buildPopularUrlsJs():
  """Reads two CSV files containing the popular URL data for the straight-
  counting pipeline and the Cobalt prototype pipeline respectively and uses them
  to build two JavaScript strings defining DataTables containing the data.

 Returns:
    {tuple of two strings} (sc_string, cobalt_string). Each of the two strings
    is of the form <var_name>=<json>, where |json| is a json string defining
    a data table. The |var_name|s are respectively
    POPULAR_URLS_SC_JS_VAR_NAME and POPULAR_URLS_JS_VAR_NAME.
  """
  # straight-counting
  popular_urls_sc_js,_ = buildItemAndCountJs(
      file_util.POPULAR_URLS_CSV_FILE_NAME, POPULAR_URLS_SC_JS_VAR_NAME, None,
      "url", "URL")

  # Cobalt.
  popular_urls_js, _ = buildItemAndCountJs(
    file_util.URL_ANALYZER_OUTPUT_FILE_NAME, POPULAR_URLS_JS_VAR_NAME, None,
    "url", "URL")

  return (popular_urls_sc_js, popular_urls_js)

def buildPopularHelpQueriesJs():
  """Reads two CSV files containing the popular help-qury data for the straight-
  counting pipeline and the Cobalt prototype pipeline respectively and uses them
  to build three JavaScript strings defining DataTables containing the data.

 Returns:
    {tuple of three strings} (sc_string, sc_histogram_string, cobalt_string).
    Each of the three strings is of the form <var_name>=<json>, where |json| is
    a json string defining a data table. The |var_name|s are respectively
    POPULAR_HELP_QUERIES_SC_JS_VAR_NAME,
    POPULAR_HELP_QUERIES_HISTOGRAM_SC_JS_VAR_NAME,
    and POPULAR_HELP_QUERIES_JS_VAR_NAME.
  """
  # straight-counting, table visualization
  popular_help_queries_sc_js, popular_help_queries_histogram_sc_js = \
      buildItemAndCountJs(file_util.POPULAR_HELP_QUERIES_CSV_FILE_NAME,
                          POPULAR_HELP_QUERIES_SC_JS_VAR_NAME,
                          POPULAR_HELP_QUERIES_HISTOGRAM_SC_JS_VAR_NAME,
                          "help_query ", "Help Query")

  # Cobalt.
  popular_help_queries_js, _ = buildItemAndCountJs(
    file_util.HELP_QUERY_ANALYZER_OUTPUT_FILE_NAME,
    POPULAR_HELP_QUERIES_JS_VAR_NAME, None,
    "help_query", "Help Query")

  return (popular_help_queries_sc_js, popular_help_queries_histogram_sc_js,
      popular_help_queries_js)

def main():
  print "Generating visualization..."

  # Read the input file and build the JavaScript strings to write.
  usage_by_module_sc_js, usage_by_module_js, usage_by_module_params_js = buildUsageByModuleJs()
  usage_by_city_js = buildUsageByCityJs()
  usage_by_hour_sc_js, usage_by_hour_js, usage_by_hour_params_js = buildUsageByHourJs()
  popular_urls_sc_js, popular_urls_js = buildPopularUrlsJs()
  (popular_help_queries_sc_js, popular_help_queries_histogram_sc_js,
      popular_help_queries_js) = buildPopularHelpQueriesJs()

  # Write the output file.
  with file_util.openForWriting(OUTPUT_JS_FILE_NAME) as f:
    f.write("// This js file is generated by the script "
            "generate_data_js.py\n\n")
    f.write("%s\n\n" % usage_by_module_sc_js)
    f.write("%s\n\n" % usage_by_module_js)
    f.write("%s\n\n" % usage_by_module_params_js)

    f.write("%s\n\n" % usage_by_city_js)

    f.write("%s\n\n" % usage_by_hour_sc_js)
    f.write("%s\n\n" % usage_by_hour_js)
    f.write("%s\n\n" % usage_by_hour_params_js)

    f.write("%s\n\n" % popular_urls_sc_js)
    f.write("%s\n\n" % popular_urls_js)

    f.write("%s\n\n" % popular_help_queries_sc_js)
    f.write("%s\n\n" % popular_help_queries_histogram_sc_js)
    f.write("%s\n\n" % popular_help_queries_js)
    f.write("")

  print "View this file in your browser:"
  print "file://%s" % file_util.VISUALIZATION_FILE

if __name__ == '__main__':
  main()
