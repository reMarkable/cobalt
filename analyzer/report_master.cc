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
#include "analyzer/schema.pb.h"
#include "analyzer/store/store.h"
#include "config/encoding_config.h"
#include "config/metric_config.h"
#include "config/report_config.h"

using cobalt::config::EncodingRegistry;
using cobalt::config::MetricRegistry;
using cobalt::config::ReportRegistry;

namespace cobalt {
namespace analyzer {

DEFINE_string(metrics, "", "Metrics definition file");
DEFINE_string(reports, "", "Reports definition file");
DEFINE_string(encodings, "", "Encodings definition file");

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
class ReportMaster {
 public:
  explicit ReportMaster(std::shared_ptr<Store> store)
      : metrics_(new MetricRegistry),
        reports_(new ReportRegistry),
        encodings_(new EncodingRegistry),
        store_(store) {}

  void Start() {
    load_configuration();

    while (1) {
      run_reports();
      sleep(10);
    }
  }

 private:
  void load_configuration() {
    if (FLAGS_metrics != "") {
      auto metrics = MetricRegistry::FromFile(FLAGS_metrics, nullptr);
      if (metrics.second != config::kOK)
        LOG(FATAL) << "Can't load metrics configuration";

      metrics_.reset(metrics.first.release());
    }

    if (FLAGS_reports != "") {
      auto reports = ReportRegistry::FromFile(FLAGS_reports, nullptr);
      if (reports.second != config::kOK)
        LOG(FATAL) << "Can't load reports configuration";

      reports_.reset(reports.first.release());
    }

    if (FLAGS_encodings != "") {
      auto encodings = EncodingRegistry::FromFile(FLAGS_encodings, nullptr);
      if (encodings.second != config::kOK)
        LOG(FATAL) << "Can't load encodings configuration";

      encodings_.reset(encodings.first.release());
    }
  }

  void run_reports() {
    LOG(INFO) << "Report cycle";

    std::unique_ptr<ReportGenerator> report_generator(
        new ReportGenerator(metrics_, reports_, encodings_, store_));

    for (const ReportConfig& config : *reports_) {
      report_generator->GenerateReport(config);
    }
  }

  std::shared_ptr<MetricRegistry> metrics_;
  std::shared_ptr<ReportRegistry> reports_;
  std::shared_ptr<EncodingRegistry> encodings_;
  std::shared_ptr<Store> store_;
};

void report_master_main() {
  LOG(INFO) << "Starting report_master";

  ReportMaster report_master(MakeStore(false));
  report_master.Start();
}

}  // namespace analyzer
}  // namespace cobalt
