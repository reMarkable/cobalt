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
#include <glog/logging.h>

#include "algorithms/forculus/forculus_encrypter.h"
#include "analyzer/analyzer_service.h"
#include "shuffler/shuffler.grpc.pb.h"
#include "config/encodings.pb.h"
#include "./observation.pb.h"

using cobalt::analyzer::kAnalyzerPort;
using cobalt::analyzer::Analyzer;
using cobalt::analyzer::ObservationBatch;
using cobalt::encoder::ClientSecret;
using cobalt::forculus::ForculusEncrypter;
using cobalt::shuffler::Envelope;
using cobalt::shuffler::Shuffler;
using cobalt::shuffler::ShufflerResponse;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using google::protobuf::Empty;

namespace cobalt {
namespace cgen {

const int kShufflerPort = 50051;

DEFINE_string(analyzer, "", "Analyzer IP");
DEFINE_string(shuffler, "", "Shuffler IP");
DEFINE_int32(num_rpcs, 1, "Number of RPCs to send");
DEFINE_int32(num_observations, 1, "Number of Observations to generate");
DEFINE_uint32(customer, 1, "Customer ID");
DEFINE_uint32(project, 1, "Project ID");
DEFINE_uint32(metric, 1, "Metric ID");
DEFINE_string(payload, "hello", "Observation payload");

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

// Encapsulates Observations.
struct GenObservation {
  Observation observation;
  EncryptedMessage encrypted;
  ObservationMetadata metadata;
};

// Generates observations and RPCs to Cobalt components
class CGen {
 public:
  CGen() : part_name_("DEFAULT")
           {}

  void setup(int argc, char *argv[]) {
    google::SetUsageMessage("Cobalt gRPC generator");
    google::ParseCommandLineFlags(&argc, &argv, true);

    customer_id_ = FLAGS_customer;
    project_id_ = FLAGS_project;
    metric_id_ = FLAGS_metric;
  }

  void start() {
    generate_observations();

    if (FLAGS_shuffler != "")
      send_shuffler();
    else if (FLAGS_analyzer != "")
      send_analyzer();
  }

 private:
  // Creates a bunch of fake observations that can be sent to shufflers or
  // analyzers.
  void generate_observations() {
    // Metadata setup.
    ObservationMetadata metadata;

    metadata.set_customer_id(customer_id_);
    metadata.set_project_id(project_id_);
    metadata.set_metric_id(metric_id_);
    metadata.set_day_index(4);

    // Forculus encode the observations.
    ForculusConfig config;
    ClientSecret client_secret = ClientSecret::GenerateNewSecret();

    config.set_threshold(10);

    ForculusEncrypter forculus(config, customer_id_, project_id_, metric_id_,
                               part_name_, client_secret);

    for (int i = 0; i < FLAGS_num_observations; i++) {
      Observation obs;
      ObservationPart part;

      part.set_encoding_config_id(1);

      ForculusObservation* forc_obs = part.mutable_forculus();
      uint32_t day_index = 0;

      if (forculus.Encrypt(FLAGS_payload, day_index, forc_obs)
          != forculus::ForculusEncrypter::kOK) {
        LOG(FATAL) << "Forculus encryption failed";
      }

      // TODO(bittau): need to specify what key-value to use for
      // single-dimension metrics.  Using DEFAULT for now.
      (*obs.mutable_parts())[part_name_] = part;

      // Encrypt the observation.
      std::string cleartext, encrypted;

      obs.SerializeToString(&cleartext);
      encrypt(cleartext, &encrypted);

      EncryptedMessage em;

      em.set_ciphertext(encrypted);

      // Add this to the list of fake observations.
      GenObservation o;
      o.observation = obs;
      o.encrypted = em;
      o.metadata = metadata;

      observations_.push_back(o);
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

    for (const GenObservation& observation : observations_) {
      // Assume all observations have the same metadata.
      *metadata = observation.metadata;

      EncryptedMessage* msg = req.add_encrypted_observation();
      *msg = observation.encrypted;
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

  void send_shuffler() {
    char dst[1024];

    snprintf(dst, sizeof(dst), "%s:%d", FLAGS_shuffler.c_str(), kShufflerPort);

    std::shared_ptr<Channel> chan =
        grpc::CreateChannel(dst, grpc::InsecureChannelCredentials());

    std::unique_ptr<Shuffler::Stub> shuffler(Shuffler::NewStub(chan));

    // Build analyzer URL.
    snprintf(dst, sizeof(dst), "%s:%d", FLAGS_analyzer.c_str(), kAnalyzerPort);

    // Build the messages to send to the shuffler.
    std::vector<EncryptedMessage> messages;

    for (const GenObservation& observation : observations_) {
      Envelope envelope;

      envelope.set_allocated_observation_meta_data(
          new ObservationMetadata(observation.metadata));

      envelope.set_allocated_encrypted_message(
          new EncryptedMessage(observation.encrypted));

      envelope.set_recipient_url(dst);

      // Encrypt the envelope.
      std::string cleartext, encrypted;

      envelope.SerializeToString(&cleartext);
      encrypt(cleartext, &encrypted);

      EncryptedMessage em;
      em.set_ciphertext(encrypted);

      messages.push_back(em);
    }

    // send RPCs.
    Timer t;
    t.start();

    auto msg_iter = messages.begin();

    if (msg_iter == messages.end())
      LOG(FATAL) << "Need messags";

    for (int i = 0; i < FLAGS_num_rpcs; i++) {
      ClientContext context;
      ShufflerResponse resp;

      Status status = shuffler->Process(&context, *msg_iter, &resp);

      if (!status.ok())
        errx(1, "error sending RPC: %s", status.error_message().c_str());

      if (++msg_iter == messages.end())
        msg_iter = messages.begin();
    }

    t.stop();

    printf("Took %lu ms for %d requests\n",
           t.elapsed() / 1000UL, FLAGS_num_rpcs);
  }

  void encrypt(const std::string& in, std::string* out) {
    // TODO(pseudorandom): please implement
    *out = in;
  }

  int customer_id_;
  int project_id_;
  int metric_id_;
  std::string part_name_;
  std::vector<GenObservation> observations_;
};  // class CGen

}  // namespace cgen
}  // namespace cobalt

int main(int argc, char *argv[]) {
  cobalt::cgen::CGen cgen;

  cgen.setup(argc, argv);
  cgen.start();

  exit(0);
}
