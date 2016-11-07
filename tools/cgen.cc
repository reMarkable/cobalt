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

#include "analyzer/analyzer_service.h"
#include "./observation.pb.h"

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
DEFINE_int32(num_observations, 1, "Number of Observations per RPC");

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
    generate_observations();

    if (FLAGS_analyzer != "")
      send_analyzer();
  }

 private:
  // Creates a bunch of fake observations that can be sent to shufflers or
  // analyzers.
  void generate_observations() {
    for (int i = 0; i < FLAGS_num_observations; i++) {
      Observation obs;
      ObservationPart part;

      part.set_encoding_config_id(1);

      BasicRapporObservation* rappor = part.mutable_basic_rappor();
      rappor->set_data("basic rappor");

      // TODO(bittau): need to specify what key-value to use for
      // single-dimension metrics.  Using DEFAULT for now.
      (*obs.mutable_parts())["DEFAULT"] = part;

      observations_.push_back(obs);
    }
  }

  // Send observations to the analyzer.
  void send_analyzer() {
    // connect to the analyzer
    char dst[1024];

    snprintf(dst, sizeof(dst), "%s:%d", FLAGS_analyzer.c_str(), kAnalyzerPort);

    std::shared_ptr<Channel> chan =
        grpc::CreateChannel(dst, grpc::InsecureChannelCredentials());

    std::unique_ptr<Analyzer::Stub> analyzer(Analyzer::NewStub(chan));

    // Generate the observation batch.
    ObservationBatch req;
    ObservationMetadata* metadata = req.mutable_meta_data();

    metadata->set_customer_id(1);
    metadata->set_project_id(2);
    metadata->set_metric_id(3);
    metadata->set_day_index(4);

    for (const Observation& observation : observations_) {
      std::string cleartext, encrypted;

      observation.SerializeToString(&cleartext);
      encrypt(cleartext, &encrypted);

      EncryptedMessage* msg = req.add_encrypted_observation();
      msg->set_ciphertext(encrypted);
    }

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

  void encrypt(const std::string& in, std::string* out) {
    // TODO(pseudorandom): please implement
    *out = in;
  }

  std::vector<Observation> observations_;
};  // class CGen

}  // namespace cgen
}  // namespace cobalt

int main(int argc, char *argv[]) {
  cobalt::cgen::CGen cgen;

  cgen.setup(argc, argv);
  cgen.start();

  exit(0);
}
