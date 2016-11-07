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

#ifndef COBALT_ALGORITHMS_ANALYZER_FORCULUS_DECRYPTER_H_
#define COBALT_ALGORITHMS_ANALYZER_FORCULUS_DECRYPTER_H_

#include <string>
#include <utility>
#include <vector>

#include "./encodings.pb.h"
#include "./observation.pb.h"

namespace cobalt {
namespace forculus {

// Decrypts a set of Forculus observations with the same ciphertext,
// if the number of observations exceeds the threshold. This is intended for use
// on the Cobalt Analyzer.
//
// usage:
// Construct a ForculusDecrypter with a |config| and |ciphertext|.
// Then invoke AddPoint() multiple times to add the set of
// points on the curve associated with the ciphertext. Finally invoke Decrypt().
class ForculusDecrypter {
 public:
  enum Status {
    kOK = 0,
    kInvalidInput,
    kNotEnoughPoints,
  };

  ForculusDecrypter(const ForculusConfig& config, std::string ciphertext);

  // Adds a point on the polynomial curve to the set. The threshold
  // is defined by the |config| passed in to the consturcutor. If at least
  // |threshold| many different points are added then Decrypt() may be invoked.
  //
  // Returns kOK on success or KInvalidInput if either |x| or |y| does not
  // represent a point in the Forculus field.
  Status AddPoint(std::string x, std::string y);

  // Decrypts the |ciphertext| that was passed to the consturctor and writes
  // the plain text to *plain_text_out. Returns kOk on success. If there are
  // not enough points to perform the decryption, returns kNotEnoughPoints.
  // If the number of points added is strictly greater than the threshold T,
  // this method will perform a validation check by comparing two different
  // randomly selected sets of points of size T. If the two different sets do
  // not yield the same result, the error kInvalidInput is returned.
  Status Decrypt(std::string *plain_text_out);

 private:
  // A point on a polynomial curve over the Forculus field.
  struct Point {
    // Move Constructor. Moves other's strings into the constructed point.
    Point(Point&& other)
        : x(std::move(other.x)), y(std::move(other.y)) {}

    // Construct a ForculusPoint by moving a pair of strings into the
    // constructed point.
    Point(std::string&& x_in, std::string&& y_in)
        : x(std::move(x_in)), y(std::move(y_in)) {}

    std::string x;
    std::string y;
  };

  ForculusConfig config_;
  std::string ciphertext_;
  std::vector<Point> points_;
};

}  // namespace forculus

}  // namespace cobalt

#endif  // COBALT_ALGORITHMS_ANALYZER_FORCULUS_DECRYPTER_H_
