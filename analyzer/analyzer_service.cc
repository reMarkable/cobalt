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

// The analyzer collector process receives reports via gRPC and stores them
// persistently.

#include "analyzer/analyzer_service.h"

#include <sys/time.h>
#include <glog/logging.h>

#include <string>

#include "analyzer/store/store.h"
#include "util/crypto_util/random.h"

using google::protobuf::Empty;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

namespace cobalt {
namespace analyzer {

Status AnalyzerServiceImpl::AddObservations(ServerContext* context,
                                            const ObservationBatch* request,
                                            Empty* response) {
  // Add a row for every observation
  for (const EncryptedMessage& em : request->encrypted_observation()) {
    ObservationKey obs_key(request->meta_data());
    std::string key = obs_key.MakeKey();
    std::string val;

    em.SerializeToString(&val);

    // TODO(bittau): need to store metadata somehow.  Right now it's implicit in
    // the row_key but that's bad design.
    store_->put(key, val);
  }

  return Status::OK;
}

void AnalyzerServiceImpl::Start() {
  ServerBuilder builder;
  char port[1024];

  snprintf(port, sizeof(port), "0.0.0.0:%d", kAnalyzerPort);

  builder.AddListeningPort(port, grpc::InsecureServerCredentials());
  builder.RegisterService(this);

  server_ = builder.BuildAndStart();
}

void AnalyzerServiceImpl::Shutdown() {
  server_->Shutdown();
}

void AnalyzerServiceImpl::Wait() {
  server_->Wait();
}

//
// ObservationKey implementation
//
ObservationKey::ObservationKey(const ObservationMetadata& meta) {
  customer_ = meta.customer_id();
  project_ = meta.project_id();
  metric_ = meta.metric_id();
  day_ = meta.day_index();

  struct timeval tv;
  gettimeofday(&tv, NULL);
  rx_time_ = tv.tv_sec * 1000000UL + tv.tv_usec;

  cobalt::crypto::Random random;
  rnd_ = random.RandomUint64();
}

std::string ObservationKey::MakeKey() {
  char out[128];

  // TODO(bittau): the key should be binary (e.g., a big-endian encoding of the
  // struct representing the key).  Right now it's human readable for easy
  // debugging.
  snprintf(out, sizeof(out), "%.10u:%.10u:%.10u:%.10u:%.20lu:%.20lu",
           customer_,
           project_,
           metric_,
           day_,
           rx_time_,
           rnd_);

  return std::string(out);
}

//
// Main entry point of the analyzer servie.
//
void analyzer_service_main() {
  LOG(INFO) << "Starting Analyzer service";

  AnalyzerServiceImpl analyzer(MakeStore(true));
  analyzer.Start();
  analyzer.Wait();
}

}  // namespace analyzer
}  // namespace cobalt
