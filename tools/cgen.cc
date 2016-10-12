// Copyright 2016 The Fuchsia Authors
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

// Cobalt traffic generator.
//
// This is a test and debug client to send data to cobalt components

#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <stdlib.h>
#include <assert.h>

#include <gflags/gflags.h>

#include "analyzer/analyzer.h"

using cobalt::analyzer::kAnalyzerPort;
using cobalt::analyzer::Analyzer;
using cobalt::analyzer::ObservationBatch;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using google::protobuf::Empty;

namespace cobalt {
namespace cgen {

DEFINE_string(analyzer, "", "Analyzer IP");
DEFINE_int32(num_rpcs, 1, "Number of RPCs to send");

// Measures time between start and stop.  Useful for benchmarking.
class Timer {
 public:
  Timer() : start_(0), stop_(0) {}

  void start() {
    start_ = now();
  }

  void stop() {
    stop_ = now();
  }

  // returns time in microseconds
  uint64_t now() {
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return tv.tv_sec * 1000UL * 1000UL + tv.tv_usec;
  }

  uint64_t elapsed() {
    assert(start_ && stop_);
    assert(stop_ >= start_);

    return stop_ - start_;
  }

 private:
  uint64_t start_;
  uint64_t stop_;
};  // class Timer

// Generates observations and RPCs to Cobalt components
class CGen {
 public:
  void setup(int argc, char *argv[]) {
    google::SetUsageMessage("Cobalt gRPC generator");
    google::ParseCommandLineFlags(&argc, &argv, true);
  }

  void start() {
    if (FLAGS_analyzer != "")
      do_analyzer();
  }

 private:
  void do_analyzer() {
    // connect to the analyzer
    char dst[1024];

    snprintf(dst, sizeof(dst), "%s:%d", FLAGS_analyzer.c_str(), kAnalyzerPort);

    std::shared_ptr<Channel> chan =
        grpc::CreateChannel(dst, grpc::InsecureChannelCredentials());

    std::unique_ptr<Analyzer::Stub> analyzer(Analyzer::NewStub(chan));

    // generate observations
    ObservationBatch req;

    EncryptedMessage* msg = req.add_encrypted_message();
    generate_msg(msg);

    // send RPCs
    Timer t;
    t.start();

    for (int i = 0; i < FLAGS_num_rpcs; i++) {
      ClientContext context;
      Empty resp;

      Status status = analyzer->AddObservations(&context, req, &resp);

      if (!status.ok())
        errx(1, "error sending RPC: %s", status.error_message().c_str());
    }

    t.stop();

    printf("Took %lu ms for %d requests\n",
           t.elapsed() / 1000UL, FLAGS_num_rpcs);
  }

  void generate_msg(EncryptedMessage *msg) {
    msg->set_ciphertext("test");
  }
};  // class CGen

}  // namespace cgen
}  // namespace cobalt

int main(int argc, char *argv[]) {
  cobalt::cgen::CGen cgen;

  cgen.setup(argc, argv);
  cgen.start();

  exit(0);
}
