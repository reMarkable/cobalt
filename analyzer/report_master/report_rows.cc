// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_rows.h"
#include "glog/logging.h"

namespace cobalt {
namespace analyzer {

ReportRowVectorIterator::ReportRowVectorIterator(
    const std::vector<ReportRow>* rows)
    : rows_(rows) {
  CHECK(rows);
  pos_ = rows_->begin();
}

ReportRowVectorIterator::ReportRowVectorIterator(
    const std::vector<ReportRow> rows)
    : owned_rows_(std::move(rows)), rows_(&owned_rows_), pos_(rows_->begin()) {}

grpc::Status ReportRowVectorIterator::Reset() {
  pos_ = rows_->begin();
  return grpc::Status::OK;
}

grpc::Status ReportRowVectorIterator::NextRow(const ReportRow** row) {
  if (!row) {
    return grpc::Status(grpc::INVALID_ARGUMENT, "row is NULL");
  }
  if (pos_ == rows_->end()) {
    return grpc::Status(grpc::NOT_FOUND, "EOF");
  }
  *row = &(*pos_++);
  return grpc::Status::OK;
}

bool ReportRowVectorIterator::HasMoreRows() { return pos_ != rows_->end(); }

}  // namespace analyzer
}  // namespace cobalt
