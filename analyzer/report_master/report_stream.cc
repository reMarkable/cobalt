// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/report_stream.h"

#include <memory>
#include <streambuf>
#include <vector>

#include "glog/logging.h"

namespace cobalt {
namespace analyzer {

// In the c++ standard library a std::stream is implemented in terms of an
// underlying std::streambuf. Therefore in order for us to implement
// ReportStream what we really have to do is implement this class,
// ReportStreamBuf, a class that derives from std::streambuf.
//
// In order to subclass std::streambuf the main methods that need to be
// overriden are underflow(), if this streambuf will be used with an
// istream, and overflow(), if this streambuf will be used with an ostream.
// A ReportStreambuf will be used with both an ostream and an istream and
// so we implement both overflow() and underflow().
//
// A ReportStreamBuf is a std::streambuf that contains a ReportSerializer*,
// a ReportRowIterator*, and a std::vector<char> buffer_. On underflow, when
// more bytes are needed by a reader, it asks the ReportSerializer to read
// more rows from the ReportRowIterator. On overflow, when more space is needed
// by a writer, it increases the size of buffer_.
class ReportStream::ReportStreambuf : public std::streambuf {
 public:
  // Constructor.
  // Does not take ownership of report_serializer or row_iterator which both
  // must remain valid as long as this instance is being used.
  // |max_size| will be used to control how many additional rows will be
  // read from |row_iterator| on underflow. This value will be passed to the
  // method ReportSerializer::AppendRows().
  ReportStreambuf(size_t max_size, ReportSerializer* report_serializer,
                  ReportRowIterator* row_iterator);

  virtual ~ReportStreambuf() = default;

  // Start should be invoked once before any other method. Returns OK on
  // success or an error status otherwise.
  grpc::Status Start();

  // Returns the most recent Status of an underlying operation that returns
  // a Status.
  grpc::Status status() { return status_; }

  // Returns the MIME type of the report being serialized. This accessor
  // may be invoked as long as Start() returned OK.
  std::string mime_type() { return report_serializer_->mime_type(); }

 protected:
  int overflow(int value) override;

  int underflow() override;

  pos_type seekoff(off_type off, std::ios_base::seekdir dir,
                   std::ios_base::openmode which) override;

 private:
  std::vector<char> buffer_;
  size_t max_size_;
  ReportSerializer* report_serializer_;  // not owned
  ReportRowIterator* row_iterator_;      // not owned
  grpc::Status status_;
};

ReportStream::ReportStreambuf::ReportStreambuf(
    size_t max_size, ReportSerializer* report_serializer,
    ReportRowIterator* row_iterator)
    : buffer_(1024),
      max_size_(max_size),
      report_serializer_(report_serializer),
      row_iterator_(row_iterator),
      status_(grpc::Status::OK) {
  CHECK(report_serializer);
  CHECK(row_iterator);
  // Tell any readers there is nothing to read.
  setg(0, 0, 0);
  // Tell any writers where the underlying write buffer is.
  setp(buffer_.data(), buffer_.data() + buffer_.size());
}

grpc::Status ReportStream::ReportStreambuf::Start() {
  // Make a temporary ostream to wrap this buffer.
  std::ostream ostream(this);
  // Give this ostream to ReportSerializer so it will write into this buffer.
  // Ask the ReportSerializer to write the header row.
  status_ = report_serializer_->StartSerializingReport(&ostream);
  if (!status_.ok()) {
    return status_;
  }
  // Ask the ReportSerializer to wite some of the report rows, up to max_size_.
  status_ = report_serializer_->AppendRows(max_size_, row_iterator_, &ostream);

  // Tell any readers where the underlying read buffer is. pptr() is the next
  // position in the write buffer and so in our case it is one past the end
  // of the read buffer.
  setg(buffer_.data(), buffer_.data(), pptr());
  return status_;
}

// This method is invoked if, while the ReportSerializer is writing into this
// buffer, we run out of space in our underlying |buffer_|. In this case
// we double the size of |buffer_|.
int ReportStream::ReportStreambuf::overflow(int value) {
  size_t previous_size = buffer_.size();
  size_t new_size = previous_size + previous_size;
  buffer_.resize(new_size);
  setp(buffer_.data() + previous_size, buffer_.data() + new_size);
  if (!traits_type::eq_int_type(value, traits_type::eof())) {
    // If the writer include one byte that should be written to the newly
    // allocated space then write it.
    sputc(value);
  }
  // Return anything other then eof to indicate success.
  return traits_type::not_eof(value);
}

// This method is invoked if, while somebody is reading from this buffer,
// we run out of data to read. In this case we serialize more of the report
// into the buffer, or return EOF.
int ReportStream::ReportStreambuf::underflow() {
  if (!status_.ok() || !row_iterator_->HasMoreRows()) {
    // Tell the reader there is no more data.
    setg(0, 0, 0);
    return traits_type::eof();
  }
  // Tell any writers where the underlying write buffer is.
  setp(buffer_.data(), buffer_.data() + buffer_.size());
  // Make a temporary ostream to wrap this buffer.
  std::ostream ostream(this);
  // Ask the ReportSerializer to wite more of the report rows, up to max_size_,
  // into the ostream and so into this buffer.
  status_ = report_serializer_->AppendRows(max_size_, row_iterator_, &ostream);
  // If any new data was written to this buffer then tell the reader about
  // the new data available to read.
  if (pptr() > buffer_.data()) {
    setg(buffer_.data(), buffer_.data(), pptr());
    return buffer_[0];
  }
  // Tell the reader there is no more data.
  setg(0, 0, 0);
  return traits_type::eof();
}

// The base class std::streambuf always return pos_type(-1) for seekoff(). We
// override this behavior only in one special case: the case in which this
// function is being invoked from std::ostream::tellp(). That is because we
// want for a ReportStream to support the tellp() function because tellp()
// is invoked by ReportSerializer::AppendRows() in order to determine how many
// bytes have already been written to the stream. According to the
// documentation for tellp(), seekoff() is invoked with parameters
// (0, cur, out), meaning that the write pointer should be moved to an offset
// of 0 from its current position and that position should be returned. In
// other words, the write pointer should not be moved at all, but its
// current position should be returned. So here we check that the parameters
// are (0, cur, out) and if they are we return the current position of the
// write pointer.
ReportStream::ReportStreambuf::pos_type ReportStream::ReportStreambuf::seekoff(
    off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which) {
  if (off == 0 && dir == std::ios_base::cur && which == std::ios_base::out) {
    // We are in the case of tellp() so return the current position of the
    // write pointer pptr().
    return pptr() - buffer_.data();
  }
  // We are not in the case of tellp() so do what the base class impl does.
  return pos_type(-1);
}

ReportStream::ReportStream(ReportSerializer* report_serializer,
                           ReportRowIterator* row_iterator, size_t buffer_size)
    : std::istream(nullptr),
      report_stream_buf_(
          new ReportStreambuf(buffer_size, report_serializer, row_iterator)) {
  // Give a pointer to the streambuf to the parent class.
  rdbuf(report_stream_buf_.get());
}

ReportStream::~ReportStream() {}

grpc::Status ReportStream::Start() { return report_stream_buf_->Start(); }

grpc::Status ReportStream::status() { return report_stream_buf_->status(); }

std::string ReportStream::mime_type() {
  return report_stream_buf_->mime_type();
}

}  // namespace analyzer
}  // namespace cobalt
