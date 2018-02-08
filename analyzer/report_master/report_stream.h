// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_STREAM_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_STREAM_H_

#include <memory>
#include <streambuf>
#include <string>
#include <utility>

#include "analyzer/report_master/report_row_iterator.h"
#include "analyzer/report_master/report_serializer.h"
#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {

// A ReportStream is an input stream from which one may read a serialized
// report. A ReportStream contains a ReportSerializer and a ReportRowIterator.
// As more bytes are read from the ReportStream, more rows are read from the
// ReportRowIterator and serialized using the ReportSerializer. In this way it
// is possible to serialize a report without having the whole report in memory
// at once.
//
// Usage:
// Construct an instance of ReportStream and invoke Start(). Then read a
// serialized report from the ReportStream using any of the standard techniques
// for working with a std::istream. After reading, or at any point during
// reading, invoke status() to see if any error has occurred with either
// reading rows of the report from the ReportIterator or with serializing
// the report.
class ReportStream : public std::istream {
 public:
  // Constructor.
  //
  // |report_serializer|. This will be used to serialize the report
  // incrementally as bytes are read from this ReportStream. The caller
  // maintains ownership of report_serializer which must remain valid
  // as long as the ReportStream is being used.
  //
  // |row_iterator|. Rows of the report will be read from this incrementally
  // as bytes are read from this ReportStream. The caller maintains ownership
  // of row_iterator which must remain valid as long as the ReportStream
  // is being used.
  //
  // |buffer_size|. This value is used to control how many additional rows
  // will be read from |row_iterator| whenever additional rows need to be
  // read because a reader has consumed all of the bytes currently buffered
  // in this ReportStream. This value will be passed as the |max_bytes|
  // parameter to the method ReportSerializer::AppendRows(). Optional. Defaults
  // to 1 MB.
  explicit ReportStream(ReportSerializer* report_serializer,
                        ReportRowIterator* row_iterator,
                        size_t buffer_size = 1024 * 1024);

  virtual ~ReportStream();

  // Invoke this method once before commencing reading from this Stream.
  // After this method has been invoked the MIME type of the report may
  // be retrieved via the accessor mime_type(). Returns OK on success or an
  // error status otherwise.
  grpc::Status Start();

  // Returns the MIME type of the report being serialized. This accessor
  // may be invoked as long as Start() returned OK.
  std::string mime_type();

  // Returns the current status. Check this after reading the whole report or
  // at any point during reading the report. If the status is not OK then
  // an error occurred either with reading rows from the ReportRowIterator or
  // with serializing the rows. In this case the bytes read from this
  // ReportStream should not be considered a valid report serialization.
  // If after reading the whole report the status is OK then there were no
  // errors.
  grpc::Status status();

 private:
  class ReportStreambuf;

  static ReportStreambuf* InitBuffer(ReportSerializer* report_serializer,
                                     ReportRowIterator* row_iterator,
                                     size_t buffer_size);

  std::unique_ptr<ReportStreambuf> report_stream_buf_;
};

}  // namespace analyzer
}  // namespace cobalt
#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_STREAM_H_
