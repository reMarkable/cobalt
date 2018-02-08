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
  //
  // |owning_istream| should be a back pointer to the ReportStream that was
  // constructed using this streambuf. It will be used to set the fail and
  // error bits when an error is returned by the ReportRowIterator or
  // ReportSerializer.
  grpc::Status Start(ReportStream* owning_istream);

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

  pos_type seekpos(pos_type pos, std::ios_base::openmode which) override;

 private:
  // Reset the stream to the begining.
  grpc::Status Reset();

  // A helper method invoked by Start() and Reset().
  grpc::Status StartSerializingReport();

  std::vector<char> buffer_;
  size_t max_size_;
  ReportSerializer* report_serializer_;  // not owned
  ReportRowIterator* row_iterator_;      // not owned
  ReportStream* owning_istream_;  // not owned. Set via the Start() method.
  grpc::Status status_;
  // Has underflow() been invoked at least once since that last
  // StartSerializingReport()? We need to keep track of this in order to
  // understand if any data at all has been read from this input stream yet.
  bool underflow_invoked_ = false;
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

grpc::Status ReportStream::ReportStreambuf::Start(
    ReportStream* owning_istream) {
  CHECK(owning_istream);
  owning_istream_ = owning_istream;
  return StartSerializingReport();
}

grpc::Status ReportStream::ReportStreambuf::Reset() {
  // Don't do anything if no data has yet been read from this buffer. We don't
  // want to issue another Bigtable query via RawDumpReportRowIterator.Reset()
  // just to fill our buffer with the same data that is already in it.
  if (!underflow_invoked_ && (gptr() == buffer_.data())) {
    return grpc::Status::OK;
  }
  row_iterator_->Reset();
  return StartSerializingReport();
}

grpc::Status ReportStream::ReportStreambuf::StartSerializingReport() {
  // Tell any readers there is nothing to read yet.
  setg(0, 0, 0);
  // Reset underflow_invoked_.
  underflow_invoked_ = false;
  // Tell any writers where the underlying write buffer is.
  setp(buffer_.data(), buffer_.data() + buffer_.size());
  // Make a temporary ostream to wrap this buffer.
  std::ostream ostream(this);
  // Give this ostream to ReportSerializer so it will write into this buffer.
  // Ask the ReportSerializer to write the header row.
  status_ = report_serializer_->StartSerializingReport(&ostream);
  if (!status_.ok()) {
    owning_istream_->setstate(std::ios_base::failbit);
    owning_istream_->setstate(std::ios_base::badbit);
    return status_;
  }

  // Ask the ReportSerializer to wite some of the report rows, up to max_size_.
  status_ = report_serializer_->AppendRows(max_size_, row_iterator_, &ostream);
  if (!status_.ok()) {
    owning_istream_->setstate(std::ios_base::failbit);
    owning_istream_->setstate(std::ios_base::badbit);
    // Note that we don't return here because even though there was an error
    // it is convenient to allow a reader to read the data that was written
    // before the error.
  }

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
  underflow_invoked_ = true;
  if (!status_.ok()) {
    // Tell the reader there is no more data.
    setg(0, 0, 0);
    return traits_type::eof();
  }
  bool has_more_rows;
  status_ = row_iterator_->HasMoreRows(&has_more_rows);
  if (!status_.ok()) {
    owning_istream_->setstate(std::ios_base::failbit);
    owning_istream_->setstate(std::ios_base::badbit);
  }
  if (!status_.ok() || !has_more_rows) {
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

  if (!status_.ok()) {
    owning_istream_->setstate(std::ios_base::failbit);
    owning_istream_->setstate(std::ios_base::badbit);
    // Note that we don't return here because even though there was an error
    // it is convenient to allow a reader to read the data that was written
    // before the error.
  }

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
// override this behavior only in two special cases: the case in which this
// function is being invoked from std::ostream::tellp() or
// std::istream::tellg(). Both of these methods may be invoked during
// report exporting. For example tellp() is invoked by
// ReportSerializer::AppendRows() in order to determine how many
// bytes have already been written to the stream.
//
// According to the documentation for tellp(), seekoff() is invoked with
// parameters (0, cur, out), meaning that the write pointer should be moved to
// an offset of 0 from its current position and that position should be
// returned. In other words, the write pointer should not be moved at all,
// but its current position should be returned.
//
// According to the documentation for tellg(), seekoff() is invoked with
// parameters (0, cur, in), meaning that the read pointer should be moved to
// an offset of 0 from its current position and that position should be
// returned. In other words, the read pointer should not be moved at all,
// but its current position should be returned.
//
// So here we check that we are in one of those two cases and we return the
// current position of the read or write pointer as appropriate.
ReportStream::ReportStreambuf::pos_type ReportStream::ReportStreambuf::seekoff(
    off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which) {
  if (off == 0 && dir == std::ios_base::cur && which == std::ios_base::out) {
    // We are in the case of tellp() so return the current position of the
    // put pointer pptr().
    return pptr() - buffer_.data();
  }
  if (off == 0 && dir == std::ios_base::cur && which == std::ios_base::in) {
    // We are in the case of tellg() so return the current position of the
    // get pointer gptr().
    return gptr() - buffer_.data();
  }
  // We are not in any of the other cases so do what the base class impl does.
  return pos_type(-1);
}

// The base class std::streambuf always return pos_type(-1) for seekpos(). We
// override this behavior only in one special case: the case in which this
// function is being invoked from std::istream::seekg(0). This is invoked
// during report exporting in the case that the google-api-cpp client
// receives a "401 authorization required" response from the Google server and
// it therefore needs to perform a reset to start reading from the beginning of
// the stream again.
//
// According to the documentation for seekg(), in the case of seekg(0),
// seekpos() is invoked with parameters (0, in), so here we check that we are
// in that case and return -1 otherwise.
ReportStream::ReportStreambuf::pos_type ReportStream::ReportStreambuf::seekpos(
    pos_type pos, std::ios_base::openmode which) {
  if (pos == 0 && which == std::ios_base::in) {
    // We are in the case of seekg(0);
    Reset();
    return 0;
  }

  // We are not in any of the cases we wish to support so do what the base
  // class impl does.
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

grpc::Status ReportStream::Start() { return report_stream_buf_->Start(this); }

grpc::Status ReportStream::status() { return report_stream_buf_->status(); }

std::string ReportStream::mime_type() {
  return report_stream_buf_->mime_type();
}

}  // namespace analyzer
}  // namespace cobalt
