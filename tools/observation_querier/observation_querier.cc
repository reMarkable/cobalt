// Copyright 2017 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tools/observation_querier/observation_querier.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "./observation.pb.h"
#include "analyzer/store/bigtable_store.h"
#include "analyzer/store/observation_store.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "util/crypto_util/base64.h"

namespace cobalt {

using analyzer::store::BigtableStore;
using analyzer::store::ObservationStore;
using crypto::Base64Encode;

DEFINE_uint32(customer, 1, "Customer ID");
DEFINE_uint32(project, 1, "Project ID");
DEFINE_bool(interactive, true,
            "If true the program runs an interactive command-loop. Otherwise a "
            "single query is performed and the count of observations returned "
            "is written to std out.");
DEFINE_uint32(metric, 1,
              "Which metric to query. Used in non-interactive mode only.");
DEFINE_uint32(max_num, 100,
              "Maximum number of results to query for. Used in non-interactive "
              "mode only.");

namespace {
// Given a |line| of text, breaks it into tokens separated by white space.
std::vector<std::string> Tokenize(const std::string& line) {
  std::istringstream line_stream(line);
  std::vector<std::string> tokens;
  do {
    std::string token;
    line_stream >> token;
    std::remove(token.begin(), token.end(), ' ');
    if (!token.empty()) {
      tokens.push_back(token);
    }
  } while (line_stream);
  return tokens;
}

void PrintHelp(std::ostream* ostream) {
  *ostream << std::endl;
  *ostream << "Cobalt ObservationStore query client" << std::endl;
  *ostream << "------------------------------------" << std::endl;
  *ostream << "help                     \tPrint this help message."
           << std::endl;
  *ostream << "query <max_num>          \tQuery up to <max_num> observations."
           << std::endl;
  *ostream << std::endl;
  *ostream << "ls                       \tList current values of "
              "parameters."
           << std::endl;
  *ostream << "set project <id>         \tSet project id." << std::endl;
  *ostream << "set metric <id>          \tSet metric id." << std::endl;
  *ostream << "quit                     \tQuit." << std::endl;
  *ostream << std::endl;
}

bool IsSet(const std::string& data, int bit_index) {
  uint32_t num_bytes = data.size();
  uint32_t byte_index = bit_index / 8;
  uint32_t bit_in_byte_index = bit_index % 8;
  return data[num_bytes - byte_index - 1] & (1 << bit_in_byte_index);
}

std::string DataToBinaryString(const std::string& data) {
  size_t num_bits = data.size() * 8;
  // Initialize output to a string of all zeroes.
  std::string output(num_bits, '0');
  size_t output_index = 0;
  for (int bit_index = num_bits - 1; bit_index >= 0; bit_index--) {
    if (IsSet(data, bit_index)) {
      output[output_index] = '1';
    }
    output_index++;
  }
  return output;
}

std::string ToString(const ValuePart& value) {
  std::ostringstream stream;
  switch (value.data_case()) {
    case ValuePart::kStringValue:
      stream << "\"" << value.string_value() << "\"";
      break;
    case ValuePart::kIntValue:
      stream << value.int_value();
      break;
    case ValuePart::kBlobValue:
      stream << "<blob of length " << value.blob_value().size() << ">";
      break;
    case ValuePart::DATA_NOT_SET:
      stream << "<ERROR: Invalid ValuePart message!>";
  }
  return stream.str();
}

std::string ToString(const ForculusObservation& obs) {
  std::ostringstream stream;
  stream << "forculus:";
  std::string ciphertext;
  Base64Encode(obs.ciphertext(), &ciphertext);
  stream << "ciphertext:" << ciphertext;
  std::string point_x;
  Base64Encode(obs.point_x(), &point_x);
  stream << "::point_x:" << point_x;
  return stream.str();
}

std::string ToString(const RapporObservation& obs) {
  std::ostringstream stream;
  stream << "rappor:";
  return stream.str();
}

std::string ToString(const BasicRapporObservation& obs) {
  std::ostringstream stream;
  stream << "basic_rappor:";
  stream << DataToBinaryString(obs.data());
  return stream.str();
}

std::string ToString(const UnencodedObservation& obs) {
  std::ostringstream stream;
  stream << "unencoded:";
  stream << ToString(obs.unencoded_value());
  return stream.str();
}

std::string ToString(const ObservationPart& observation_part) {
  switch (observation_part.value_case()) {
    case ObservationPart::kForculus:
      return ToString(observation_part.forculus());
    case ObservationPart::kRappor:
      return ToString(observation_part.rappor());
    case ObservationPart::kBasicRappor:
      return ToString(observation_part.basic_rappor());
    case ObservationPart::kUnencoded:
      return ToString(observation_part.unencoded());
    case ObservationPart::VALUE_NOT_SET:
      return "value not set";
  }
}

std::string ToString(const Observation& observation) {
  std::ostringstream stream;
  bool first = true;
  for (const auto& pair : observation.parts()) {
    if (!first) {
      stream << std::endl;
    }
    first = false;
    stream << pair.first << ":" << ToString(pair.second);
  }
  return stream.str();
}

}  // namespace

std::unique_ptr<ObservationQuerier> ObservationQuerier::CreateFromFlagsOrDie() {
  std::shared_ptr<ObservationStore> observation_store(
      new ObservationStore(BigtableStore::CreateFromFlagsOrDie()));
  return std::unique_ptr<ObservationQuerier>(new ObservationQuerier(
      FLAGS_customer, FLAGS_project, observation_store, &std::cout));
}

ObservationQuerier::ObservationQuerier(
    uint32_t customer_id, uint32_t project_id,
    std::shared_ptr<ObservationStore> observation_store, std::ostream* ostream)
    : customer_(customer_id),
      project_(project_id),
      observation_store_(observation_store),
      ostream_(ostream) {}

void ObservationQuerier::Run() {
  if (FLAGS_interactive) {
    CommandLoop();
    return;
  }
  CountObservations();
}

void ObservationQuerier::CommandLoop() {
  std::string command_line;
  while (true) {
    *ostream_ << "Command or 'help': ";
    getline(std::cin, command_line);
    if (!ProcessCommandLine(command_line)) {
      break;
    }
  }
}

// Counts the number of Observations in the Observation store and writes the
// count to std::cout. We iteratively query in batches of size up to 10000
// and stop counting when we have seen FLAGS_max_num observations. Thus the
// result will be <= FLAGS_max_num.
void ObservationQuerier::CountObservations() {
  size_t num_observations = 0;
  const size_t batch_size = std::min(FLAGS_max_num - num_observations, 10000ul);
  std::string pagination_token = "";
  do {
    auto query_response = observation_store_->QueryObservations(
        customer_, project_, FLAGS_metric, 0, INT32_MAX,
        std::vector<std::string>(), batch_size, pagination_token);
    if (query_response.status != analyzer::store::kOK) {
      LOG(FATAL) << "Query failed with code: " << query_response.status;
      return;
    }
    num_observations += query_response.results.size();
    pagination_token = std::move(query_response.pagination_token);
  } while (!pagination_token.empty() && num_observations < FLAGS_max_num);

  std::cout << num_observations;
}

bool ObservationQuerier::ProcessCommandLine(const std::string command_line) {
  return ProcessCommand(Tokenize(command_line));
}

bool ObservationQuerier::ProcessCommand(
    const std::vector<std::string>& command) {
  if (command.empty()) {
    return true;
  }

  if (command[0] == "help") {
    PrintHelp(ostream_);
    return true;
  }

  if (command[0] == "query") {
    Query(command);
    return true;
  }

  if (command[0] == "ls") {
    ListParameters();
    return true;
  }

  if (command[0] == "set") {
    SetParameter(command);
    return true;
  }

  if (command[0] == "quit") {
    return false;
  }

  *ostream_ << "Unrecognized command: " << command[0] << std::endl;

  return true;
}

void ObservationQuerier::Query(const std::vector<std::string>& command) {
  if (command.size() != 2) {
    *ostream_ << "Malformed query command. Expected query <max_num>"
              << std::endl;
    return;
  }
  int64_t max_num;
  if (!ParseInt(command[1], &max_num)) {
    return;
  }
  if (max_num <= 0) {
    *ostream_ << "<max_num> must be a positive integer: " << max_num
              << std::endl;
    return;
  }

  auto query_response = observation_store_->QueryObservations(
      customer_, project_, metric_, 0, INT32_MAX, std::vector<std::string>(),
      max_num, "");

  if (query_response.status != analyzer::store::kOK) {
    *ostream_ << "Query failed with code: " << query_response.status
              << std::endl;
    return;
  }

  for (const auto& query_result : query_response.results) {
    *ostream_ << ToString(query_result.observation) << std::endl;
  }
}

void ObservationQuerier::ListParameters() {
  *ostream_ << std::endl;
  *ostream_ << "Settable values" << std::endl;
  *ostream_ << "---------------" << std::endl;
  *ostream_ << "Project ID: " << project_ << std::endl;
  *ostream_ << "Metric ID: " << metric_ << std::endl;
  *ostream_ << std::endl;
  *ostream_ << "Values set by flag at startup." << std::endl;
  *ostream_ << "-----------------------------" << std::endl;
  *ostream_ << "Customer ID: " << customer_ << std::endl;
  *ostream_ << std::endl;
}

void ObservationQuerier::SetParameter(const std::vector<std::string>& command) {
  if (command.size() != 3) {
    *ostream_ << "Malformed set command. Expected 2 additional arguments."
              << std::endl;
    return;
  }

  if (command[1] == "metric") {
    int64_t id;
    if (!ParseInt(command[2], &id)) {
      return;
    }
    if (id <= 0) {
      *ostream_ << "<id> must be a positive integer";
      return;
    }
    metric_ = id;
  } else if (command[1] == "project") {
    int64_t id;
    if (!ParseInt(command[2], &id)) {
      return;
    }
    if (id <= 0) {
      *ostream_ << "<id> must be a positive integer";
      return;
    }
    project_ = id;
  } else {
    *ostream_ << command[1] << " is not a settable parameter." << std::endl;
  }
}

bool ObservationQuerier::ParseInt(const std::string& str, int64_t* x) {
  CHECK(x);
  std::istringstream iss(str);
  *x = 0;
  iss >> *x;
  char c;
  if (*x == 0 || iss.fail() || iss.get(c)) {
    *ostream_ << "Expected positive integer instead of " << str << "."
              << std::endl;
    return false;
  }
  return true;
}

}  // namespace cobalt
