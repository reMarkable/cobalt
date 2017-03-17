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

#ifndef COBALT_TOOLS_OBSERVATION_QUERIER_OBSERVATION_QUERIER_H_
#define COBALT_TOOLS_OBSERVATION_QUERIER_OBSERVATION_QUERIER_H_

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "analyzer/store/observation_store.h"

namespace cobalt {

// The engine for an interactive command-line tool used to query the
// Cobalt ObservationStore. The tools is used by Cobalt developers for
// debugging and demonstrating the Cobalt system. The engine has been abstracted
// from the main program so that it may be tested.
class ObservationQuerier {
 public:
  static std::unique_ptr<ObservationQuerier> CreateFromFlagsOrDie();

  // Construct an ObservationQuerier that will query from the given
  // |observation_store|. Interactive output will be written to |ostream|.
  // In non-test environments this is usually set to &std::cout.
  ObservationQuerier(
      uint32_t customer_id, uint32_t project_id,
      std::shared_ptr<analyzer::store::ObservationStore> observation_store,
      std::ostream* ostream);

  // Run() is invoked by main(). It invokes either CommandLoop()
  // or QueryOnce() depending on the value of the -interactive flag.
  void Run();

  // Processes a single command. The method is public so an instance of
  // ObservationQuerier may be used as a library and driven from another
  // program. We use this in unit tests. Returns false if an only if the
  // specified command is "quit".
  bool ProcessCommandLine(const std::string command_line);

 private:
  // Implements interactive mode. Reads a command
  // from standard input and invokes ProcessCommandLine() until a 'quit'
  // command is seen.
  void CommandLoop();

  // Implements non-interactive mode. Performs a query based on the flags
  // and writes the results to std out.
  void QueryOnce();

  bool ProcessCommand(const std::vector<std::string>& command);
  void Query(const std::vector<std::string>& command);
  void ListParameters();
  void SetParameter(const std::vector<std::string>& command);
  bool ParseInt(const std::string& str, int64_t* x);

  uint32_t customer_ = 1;
  uint32_t project_ = 1;
  uint32_t metric_ = 1;
  std::shared_ptr<analyzer::store::ObservationStore> observation_store_;
  std::ostream* ostream_;
};

}  // namespace cobalt

#endif  // COBALT_TOOLS_OBSERVATION_QUERIER_OBSERVATION_QUERIER_H_
