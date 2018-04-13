// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file declares objects which are used to carry the output of the cobalt
// client library. The ValuePart, ObservationPart and Observation objects
// correspond to the identically-named protobuf messages found in
// observation.proto.

#ifndef COBALT_CLIENT_COLLECTION_OBSERVATION_H_
#define COBALT_CLIENT_COLLECTION_OBSERVATION_H_

#include <functional>
#include <string>
#include <vector>

namespace cobalt {
namespace client {

// An UndoFunction is called to indicate a collection attempt has failed and
// must be undone.
typedef std::function<void()> UndoFunction;

// The value of a MetricPart to be sent to Cobalt.
// The value and type of a ValuePart cannot be changed.
class ValuePart {
 public:
  // Returns an integer value part.
  static const ValuePart MakeIntValuePart(int64_t value) {
    Value val;
    val.int_value = value;
    return ValuePart(INT, val);
  }

  enum Type {
    INT,
  };

  // Returns the type of the value part.
  Type Which() const { return type_; }

  // Returns true if the value part is an integer.
  bool IsIntValue() const { return type_ == INT; }

  // Returns the integer value of an integer value part. If the value part is
  // not an integer, the behavior is undefined.
  int64_t GetIntValue() const { return value_.int_value; }

 private:
  union Value {
    int64_t int_value;
  };

  ValuePart(Type type, Value value): type_(type), value_(value) {}

  const Type type_;
  const Value value_;
};

// An ObservationPart represents a collected observation part. It currently
// only supports integers.
struct ObservationPart {
  ObservationPart(std::string part_name, uint32_t encoding_id, ValuePart value,
                  UndoFunction undo)
      : part_name(part_name),
        encoding_id(encoding_id),
        value(value),
        undo(undo) {}

  std::string part_name;
  uint32_t encoding_id;
  ValuePart value;
  // Calling undo will undo the collection of the metric part.
  // TODO(azani): Maybe make private.
  UndoFunction undo;
};

// An Observation represents a collected observation to be sent to Cobalt.
struct Observation {
  uint32_t metric_id;
  std::vector<ObservationPart> parts;
  // Calling undo will undo the collection of the metric including its parts.
  UndoFunction undo;
};

}  // namespace client
}  // namespace cobalt

#endif  // COBALT_CLIENT_COLLECTION_OBSERVATION_H_
