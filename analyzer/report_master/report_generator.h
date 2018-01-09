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

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_GENERATOR_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_GENERATOR_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "./encrypted_message.pb.h"
#include "./observation.pb.h"
#include "algorithms/forculus/forculus_analyzer.h"
#include "analyzer/report_master/report_exporter.h"
#include "analyzer/store/observation_store.h"
#include "analyzer/store/report_store.h"
#include "config/analyzer_config.h"
#include "config/analyzer_config_manager.h"
#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {

// In Cobalt V0.1 ReportGenerator is a singleton, single-threaded object
// owned by the ReportMaster. In later versions of Cobalt, ReportGenerator
// will be a separate service.
//
// ReportGenerator is responsible for generating individual reports. It is not
// responsible for knowing anything about report schedules. It is not
// responsible for figuring out which interval of days a report should analyze.
// Those things are the responsibility of the ReportMaster.
//
// The ReportGenerator uses the ObservationStore, the ReportStore and the
// ReportExporter for its input and output. It reads ReportMetadata from the
// ReportStore, reads Observations from the ObservationStore, writes ReportRows
// to the ReportStore, and exports reports using the ReportExporter. The
// AnalyzerConfig is used to look up report and metric configs.
class ReportGenerator {
 public:
  // report_exporter is allowed to be NULL, in which case no exporting will
  // occur.
  ReportGenerator(
      std::shared_ptr<config::AnalyzerConfigManager> config_manager,
      std::shared_ptr<store::ObservationStore> observation_store,
      std::shared_ptr<store::ReportStore> report_store,
      std::unique_ptr<ReportExporter> report_exporter);

  // Requests that the ReportGenerator generate the report with the given
  // |report_id|. This method is invoked by the ReportMaster after
  // the ReportMaster invokes ReportStore::StartNewReport(). The ReportGenerator
  // will query the ReportMetadata for the report with the given |report_id|
  // from the ReportStore. The ReportMetadata must be found and must indicate
  // that the report is in the IN_PROGRESS state which is the state it is in
  // immediately after ReportMaster invokes StartNewReport().
  //
  // The |first_day_index| and |last_day_index| from the ReportMetadata
  // determine the range of day indices over which analysis will be performed.
  // Since the ReportMaster is responsible for writing the ReportMetadata via
  // the call to StartNewReport, it is the ReportMaster and not the
  // ReportGenerator that determines the interval of days that should be
  // analyzed by the report.
  //
  // The |report_config_id| field of the |report_id| specifies the ID of
  // a ReportConfig that must be found in the |analyzer_config| registry that
  // was passed to the constructor. The report being generated is an instance
  // of this ReportConfig.
  //
  // The |sequence_num| field of the |report_id| specifies the position of this
  // report in its dependency chain. If |sequence_num| is greater than zero
  // then all previous reports in the chain (that is reports with smaller
  // sequence numbers) must already have been completed.
  //
  // The ReportGenerator will read the Observations to be analyzed from the
  // ObservationStre and will write the output of the analysis into the
  // ReportStore via the method ReportStore::AddReportRows().
  //
  // This method will return when the report generation is complete. It is then
  // the responsibility of the caller (i.e. the ReportMaster) to finish
  // the report by invoking ReportStore::EndReport().
  //
  // The returned status will be OK if the report was generated successfully
  // or an error status otherwise.
  grpc::Status GenerateReport(const ReportId& report_id);

 private:
  // Represents one of the variables to be analyzed from the list of variables
  // specified in a ReportConfig.
  struct Variable {
    Variable(uint32_t index, const ReportVariable* report_variable)
        : index(index), report_variable(report_variable) {}

    Variable(const Variable& other)
        : index(other.index), report_variable(other.report_variable) {}

    // The index of the variable within the list of variables in a ReportConfig.
    uint32_t index;

    // Not owned. A pointer to the ReportVariable from the ReportConfig.
    const ReportVariable* report_variable;
  };

  // Builds the appropriate vector of Variables to analyze given the
  // input data. Writes the result into |variables|.
  // On error, does LOG(ERROR) and returns an appropriate status.
  grpc::Status BuildVariableList(const ReportConfig& report_config,
                                 const ReportId& report_id,
                                 const ReportMetadataLite& metadata,
                                 std::vector<Variable>* variables);

  // This is a helper function for GenerateReport().
  //
  // Generates the Histogram report with the given |report_id|,
  // performing the analysis over the period [first_day_index, last_day_index].
  // |report_config| must be the associated ReportConfig,
  // |metric| must be the associated Metric and |variables|
  // must be a vector of size 1 containing the single variable being analyzed.
  //
  // |in_store| specifies whether or not to save the generated report to the
  // ReportStore.
  //
  // On success, the generated report will be returned in the variable
  // |report_rows|. If |in_store| is true it will also be saved to the
  // ReportStore.
  grpc::Status GenerateHistogramReport(const ReportId& report_id,
                                       const ReportConfig& report_config,
                                       const Metric& metric,
                                       std::vector<Variable> variables,
                                       uint32_t start_day_index,
                                       uint32_t end_day_index,
                                       bool in_store,
                                       std::vector<ReportRow>* report_rows);

  std::shared_ptr<config::AnalyzerConfigManager> config_manager_;
  std::shared_ptr<store::ObservationStore> observation_store_;
  std::shared_ptr<store::ReportStore> report_store_;
  std::unique_ptr<ReportExporter> report_exporter_;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_GENERATOR_H_
