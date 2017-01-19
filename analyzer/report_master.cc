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

// The report_master periodically scans the database, decodes any observations,
// and
// publishes them.

#include "analyzer/report_master.h"

#include <glog/logging.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "algorithms/forculus/forculus_analyzer.h"
#include "analyzer/report_generator.h"
#include "analyzer/store/data_store.h"
#include "config/encoding_config.h"
#include "config/metric_config.h"
#include "config/report_config.h"

using cobalt::config::EncodingRegistry;
using cobalt::config::MetricRegistry;
using cobalt::config::ReportRegistry;

namespace cobalt {
namespace analyzer {

using store::ObservationStore;
using store::DataStore;

DEFINE_string(cobalt_config_dir, "",
              "Path to the Cobalt configuration directory (should not end with "
              "forward slash)");

class ReportMaster {
 public:
  explicit ReportMaster(std::shared_ptr<DataStore> store)
      : metrics_(new MetricRegistry),
        reports_(new ReportRegistry),
        encodings_(new EncodingRegistry),
        store_(store),
        observation_store_(new ObservationStore(store)) {}

  void Start(std::atomic<bool>* shut_down) {
    load_configuration();

    while (!(*shut_down)) {
      run_reports();
      sleep(10);
    }
  }

 private:
  // TODO(rudominer) Don't hard-code the names of the config files.
  void load_configuration() {
    CHECK(!FLAGS_cobalt_config_dir.empty())
        << "Flag --cobalt_config_dir is mandatory";

    auto encodings = EncodingRegistry::FromFile(
        FLAGS_cobalt_config_dir + "/registered_encodings.txt", nullptr);
    if (encodings.second != config::kOK) {
      LOG(FATAL) << "Can't load encoding configuration";
    }
    encodings_.reset(encodings.first.release());

    auto metrics = MetricRegistry::FromFile(
        FLAGS_cobalt_config_dir + "/registered_metrics.txt", nullptr);
    if (metrics.second != config::kOK) {
      LOG(FATAL) << "Can't load metrics configuration";
    }
    metrics_.reset(metrics.first.release());

    auto reports = ReportRegistry::FromFile(
        FLAGS_cobalt_config_dir + "/registered_reports.txt", nullptr);
    if (reports.second != config::kOK) {
      LOG(FATAL) << "Can't load reports configuration";
    }
    reports_.reset(reports.first.release());
  }

  void run_reports() {
    LOG(INFO) << "Report cycle";

    std::unique_ptr<ReportGenerator> report_generator(new ReportGenerator(
        metrics_, reports_, encodings_, observation_store_));

    for (const ReportConfig& config : *reports_) {
      report_generator->GenerateReport(config);
    }
  }

  std::shared_ptr<MetricRegistry> metrics_;
  std::shared_ptr<ReportRegistry> reports_;
  std::shared_ptr<EncodingRegistry> encodings_;
  std::shared_ptr<DataStore> store_;
  std::shared_ptr<ObservationStore> observation_store_;
};

void ReportMasterMain(std::atomic<bool>* shut_down) {
  LOG(INFO) << "Starting report_master";

  ReportMaster report_master(store::DataStore::CreateFromFlagsOrDie());
  report_master.Start(shut_down);
}

}  // namespace analyzer
}  // namespace cobalt
