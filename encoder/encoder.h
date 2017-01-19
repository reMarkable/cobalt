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

#ifndef COBALT_ENCODER_ENCODER_H_
#define COBALT_ENCODER_ENCODER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "./observation.pb.h"
#include "encoder/client_secret.h"
#include "encoder/project_context.h"

namespace cobalt {
namespace encoder {

// An Encoder is used for encoding raw values into Observations. An Observation
// is the unit of encoded data that is sent from a client to the Shuffler and
// ultimately to the Analyzer.
//
// An instance of Encoder is associated with a single customer project. Once
// constructed the instance may be used repeatedly to encode many different
// values for many different metrics in that project.
//
// Encoder offers a simple and an advanced API. The simple API may be used for
// metrics that have only a single part. The advanced API must be used for
// metrics that have multiple parts.
//
// The raw values that are inputs to an encoding are typed. We currently handle
// three different types:
// (a) UTF8, human-readable strings
// (b) Signed 64-bit integers
// (c) Uninterpretted blobs of bytes
//
// Each call to any of the Encode*() methods specifies a metric and (implicitly
// or explicitly) a metric part name. Each metric part has a type and the type
// of the value passed to Encode*() must correspond to the type of the metric
// part.
//
// Each call to any of the Encode*() methods also specifies an encoding config.
// Not every data type is compatible with every encoding type. Currently the
// following combinatations are supported:
// (a) UTF8, human-readable strings are compatible with all encoding types.
// (b) Integers are compatible with Basic RAPPOR only.
// (c) Blobs are compatible with Forculus only.
class Encoder {
 public:
  // Constructs an Encoder for the given project.
  //
  // project: A ProjectContext representing a particular Cobalt project.  All
  //     Observations produced by the constructed Encoder will be for this
  //     project. All metric_ids and encoding_config_ids passed to the
  //     Encode*() methods are interpretted relative to this project.
  //
  // client_secret: A random secret that is generated once on the client
  //     and then persisted by the client and used repeatedly. It is used  as
  //     an input by some of the encodings.
  Encoder(std::shared_ptr<ProjectContext> project, ClientSecret client_secret);

  enum Status {
    kOK = 0,

    // Returned by the Encode*() methods if an invalid ID or metric part name
    // is specified or if the number of value parts does not coincide with the
    // number of metric parts or if the data type of a value part does not
    // correspond to the data type of the corresponding MetricPart or is not
    // consistent with the specified encoding. See comments at the top of this
    // file for details.
    kInvalidArguments,

    // Returned by the Encode*() methods if the metric or encoding definitions
    // contained in the ProjectContext passed to the constructor are invalid.
    kInvalidConfig,

    // Returned by the Encode*() methods if the encoding operation failed for
    // any reason.
    kEncodingFailed
  };

  // The output of the Encode*() methods is a triple consisting of a status
  // and, if the status is kOK, an observation and its metadata.
  struct Result {
    Status status;
    std::unique_ptr<Observation> observation;
    std::unique_ptr<ObservationMetadata> metadata;
  };

  /////////////////////////////////////////////////////////////////////////////
  //                            The simple API
  //
  // This API may be used to generate encoded observations for a metric that
  // has only a single part. In this case it is not necessary to specify a
  // metric part name and it is only necessary to give a single typed value to
  // be encoded.
  ////////////////////////////////////////////////////////////////////////////

  // Encodes the utf8, human-readable string |value| using the specified
  // encoding for the specified metric. On success the result contains kOK
  // and an Observation with its metadata. Otherwise the result contains
  // an error status.
  Result EncodeString(uint32_t metric_id, uint32_t encoding_config_id,
                      const std::string& value);

  // Encodes the integer |value| using the specified encoding for the specified
  // metric. On success the result contains kOK and an Observation with its
  // metadata. Otherwise the result contains an error status.
  Result EncodeInt(uint32_t metric_id, uint32_t encoding_config_id,
                   int64_t value);

  // Encodes |num_bytes| of uninterpreted data from |data| using the specified
  // encoding for the specified metric. On success the result contains kOK
  // and an Observation with its metadata. Otherwise the result contains
  // an error status.
  Result EncodeBlob(uint32_t metric_id, uint32_t encoding_config_id,
                    const void* data, size_t num_bytes);

  /////////////////////////////////////////////////////////////////////////////
  //                            The advanced API
  //
  // This API must be used to generate encoded observations for a metric that
  // has more than one metric part. In this case a Value has multiple typed
  // parts and each part must specify both a metric part name and an encoding
  // config that it is associated with.
  ////////////////////////////////////////////////////////////////////////////

  // A multi-part value of a metric to be encoded into a mult-part Observation.
  // This is the input to Encoder::Encode(). Construct an instance of Value and
  // then repeatedly invoke the Add*() methods to add parts to the value.
  // Finally pass the value to the Encoder::Encode() method.
  class Value {
   public:
    // Adds the utf8, human-readable string |value| to this multi-part Value,
    // associates it with the metric part named |part_name| and requests that it
    // be encoded using the configuration specified by |encoding_config_id|.
    void AddStringPart(uint32_t encoding_config_id,
                       const std::string& part_name, const std::string& value);

    // Adds the integer |value| to this multi-part Value, associates
    // it with the metric part named |part_name| and requests that it be
    // encoded using the configuration specified by |encoding_config_id|.
    void AddIntPart(uint32_t encoding_config_id, const std::string& part_name,
                    int64_t value);

    // Adds |num_bytes| of uninterpreted data from |data| to this Value,
    // associates it with the metric part named |part_name| and requests that
    // it be encoded using the configuration specified by |encoding_config_id|.
    void AddBlobPart(uint32_t encoding_config_id, const std::string& part_name,
                     const void* data, size_t num_bytes);

   private:
    friend class Encoder;

    // A triple consisting of a ValuePart, an encoding_config_id and a DataType.
    struct ValuePartData {
      ValuePartData(uint32_t encoding_config_id, MetricPart::DataType data_type)
          : encoding_config_id(encoding_config_id), data_type(data_type) {}

      uint32_t encoding_config_id;
      MetricPart::DataType data_type;
      ValuePart value_part;
    };

    // Adds an additional ValuePartData to parts_ with the given name,
    // encoding_config_id and data_type. Returns a reference to the ValuePart
    // contained in the newly added ValuePartData.
    ValuePart& AddPart(uint32_t encoding_config_id,
                       const std::string& part_name,
                       MetricPart::DataType data_type);

    // The parts of this value. The keys to the map are the part names.
    std::map<std::string, ValuePartData> parts_;
  };

  // Encodes the multi-part |value| for the specified metric.  On success the
  // result contains kOK and an Observation with its metadata. Otherwise the
  // result contains an error status.
  Result Encode(uint32_t metric_id, const Value& value);

  // Sets a static value to use for the current time when computing the
  // day index. By default an Encoder uses the real system clock to determine
  // the current time. But this function may be invoked to override that
  // behavior. This is useful for example in tests. Invoke this function with
  // zero or a negative number to restore the default behavior.
  void set_current_time(time_t time) {
    current_time_ = time;
  }

 private:
  // Helper function that performs Forculus encoding on |value| using the
  // given metadata and writes the result into |observation_part|.
  Status EncodeForculus(uint32_t metric_id, uint32_t encoding_config_id,
                        MetricPart::DataType data_type, const ValuePart& value,
                        const EncodingConfig* encoding_config,
                        const std::string& part_name, uint32_t day_index,
                        ObservationPart* observation_part);

  // Helper function that performs RAPPOR encoding on |value| using the
  // given metadata and writes the result into |observation_part|.
  Status EncodeRappor(uint32_t metric_id, uint32_t encoding_config_id,
                      MetricPart::DataType data_type, const ValuePart& value,
                      const EncodingConfig* encoding_config,
                      const std::string& part_name,
                      ObservationPart* observation_part);

  // Helper function that performs Basic Forculus encoding on |value| using the
  // given metadata and writes the result into |observation_part|.
  Status EncodeBasicRappor(uint32_t metric_id, uint32_t encoding_config_id,
                           MetricPart::DataType data_type,
                           const ValuePart& value,
                           const EncodingConfig* encoding_config,
                           const std::string& part_name,
                           ObservationPart* observation_part);

  uint32_t customer_id_, project_id_;
  std::shared_ptr<ProjectContext> project_;
  ClientSecret client_secret_;
  time_t current_time_ = 0;
};

}  // namespace encoder
}  // namespace cobalt

#endif  // COBALT_ENCODER_ENCODER_H_
