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

#include "analyzer/store/report_store_test_utils.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "analyzer/store/report_store.h"

namespace cobalt {
namespace analyzer {
namespace store {

ReportStoreTestUtils::ReportStoreTestUtils(
    std::shared_ptr<ReportStore> report_store)
    : report_store_(report_store) {}

Status ReportStoreTestUtils::WriteBulkMetadata(
    const std::vector<ReportId>& report_ids,
    const std::vector<ReportMetadataLite>& metadata) {
  return report_store_->WriteBulkMetadata(report_ids, metadata);
}

}  // namespace store
}  // namespace analyzer
}  // namespace cobalt
