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

#include "analyzer/analyzer.h"

#include <sys/time.h>

#include <string>

#include "util/crypto_util/random.h"

using google::protobuf::Empty;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

namespace cobalt {
namespace analyzer {

AnalyzerServiceImpl::AnalyzerServiceImpl(Store* store)
    : store_(store) {
}

Status AnalyzerServiceImpl::AddObservations(ServerContext* context,
                                            const ObservationBatch* request,
                                            Empty* response) {
  // Add a row for every observation
  for (const EncryptedMessage& em : request->encrypted_observation()) {
    std::string key = make_row_key(request->meta_data());
    std::string val;

    em.SerializeToString(&val);

    // TODO(bittau): need to store metadata somehow.  Right now it's implicit in
    // the row_key but that's bad design.
    store_->put(key, val);
  }

  return Status::OK;
}

std::string AnalyzerServiceImpl::make_row_key(const ObservationMetadata& meta) {
  char out[128];
  uint64_t rnd = 0;
  cobalt::crypto::Random random;
  struct timeval tv;

  random.RandomBytes(reinterpret_cast<crypto::byte*>(&rnd), sizeof(rnd));
  gettimeofday(&tv, NULL);

  snprintf(out, sizeof(out), "%.10u:%.10u:%.10u:%.10u:%.20lu:%.20lu",
           meta.customer_id(),
           meta.project_id(),
           meta.metric_id(),
           meta.day_index(),
           tv.tv_sec * 1000000UL + tv.tv_usec,
           rnd);

  return std::string(out);
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

}  // namespace analyzer
}  // namespace cobalt
