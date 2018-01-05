// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_ROWS_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_ROWS_H_

#include <vector>

#include "analyzer/store/report_store.h"
#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {

// An interface for iterating over the rows of a report.
class ReportRowIterator {
 public:
  virtual ~ReportRowIterator() = default;

  // Resets the iterator to the beginning.
  // Returns OK on success or an error status.
  virtual grpc::Status Reset() = 0;

  // Fetches the next row.
  // Returns:
  // - OK if |*row| has been updated to point to the next row.
  // - NOT_FOUND if the iteration is complete and there are no more rows to
  //   return. In this case the state of |*row| is undefined.
  // - INVALID_ARGUMENT if |row| is NULL.
  // - Some other status if any other error occurs.
  virtual grpc::Status NextRow(ReportRow** row) = 0;
};

// An implementation of ReportRowIterator that wraps a vector.
class ReportRowVectorIterator : public ReportRowIterator {
 public:
  // Constructs a ReportRowVectorIterator that wraps the given vector.
  // Does not take ownership of |rows|.
  explicit ReportRowVectorIterator(std::vector<ReportRow>* rows);

  virtual ~ReportRowVectorIterator() = default;

  grpc::Status Reset() override;

  grpc::Status NextRow(ReportRow** row) override;

 private:
  std::vector<ReportRow>* rows_;  // not owned
  std::vector<ReportRow>::iterator pos_;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_ROWS_H_
