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

// The reporter periodically scans the database, decodes any observations, and
// publishes them.

#include <glog/logging.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "algorithms/forculus/forculus_analyzer.h"
#include "analyzer/analyzer_service.h"
#include "analyzer/reporter.h"
#include "analyzer/store/store.h"

#include "./observation.pb.h"

using cobalt::forculus::ForculusAnalyzer;

namespace cobalt {
namespace analyzer {

// XXX TODO(bittau):
//
// WARNING ======================================================
//
// This entire class is simply an experiment and a place holder.  It is merely
// used to tie up pieces to see how they work.  This is in no way final or
// representative of what the final outcome will look like.
//
// Eventually this class will be implemented for real.  Currently it's a
// prototype / playground only.
//
// WARNING ======================================================
//
class Reporter {
 public:
  explicit Reporter(std::unique_ptr<Store>&& store)
      : store_(std::move(store)) {}

  void Start() {
    while (1) {
      run_report();
      sleep(10);
    }
  }

 private:
  void run_report() {
      LOG(INFO) << "Report cycle";

      // Just dump the db for now
      std::map<std::string, std::string> db;
      int rc = store_->get_range("", "", &db);

      if (rc != 0) {
        LOG(ERROR) << "get_range() error: " << rc;
        return;
      }

      // Try to decode forculus strings
      ForculusConfig config;
      config.set_threshold(10);

      ForculusAnalyzer forculus(config);

      for (auto& i : db) {
        EncryptedMessage em;
        Observation obs;

        if (!em.ParseFromString(i.second)) {
          LOG(ERROR) << "Can't parse EncryptedMessage.  Key: " << i.first;
          continue;
        }

        std::string cleartext;

        if (!decrypt(em.ciphertext(), &cleartext)) {
          LOG(ERROR) << "Can't decrypt";
          continue;
        }

        if (!obs.ParseFromString(cleartext)) {
          LOG(ERROR) << "Can't parse Observation.  Key: " << i.first;
          continue;
        }

        if (obs.parts_size() < 1) {
          LOG(ERROR) << "Not enough parts: " << obs.parts_size();
          continue;
        }

        const ObservationPart& part = obs.parts().begin()->second;
        const ForculusObservation& forc_obs = part.forculus();

        if (!forculus.AddObservation(0, forc_obs)) {
          LOG(ERROR) << "Can't add observation";
          continue;
        }
      }

      // check forculus results
      auto results = forculus.TakeResults();

      for (auto& i : results) {
        LOG(INFO) << "Found plain-text:" << i.first;
      }
  }

  // TODO(pseudorandom): implement
  bool decrypt(const std::string ciphertext, std::string* cleartext) {
    *cleartext = ciphertext;

    return true;
  }

  std::unique_ptr<Store> store_;
};

void reporter_main() {
  LOG(INFO) << "Starting reporter";

  Reporter reporter(MakeStore(false));
  reporter.Start();
}

}  // namespace analyzer
}  // namespace cobalt
