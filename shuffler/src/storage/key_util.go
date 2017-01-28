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

package storage

import (
	"bytes"
	"encoding/base64"
	"encoding/binary"
	"fmt"
	"math"
	"strings"

	"github.com/golang/protobuf/proto"

	shufflerpb "cobalt"
	randutil "util"
)

// NewRowKey takes the base64 encoding of a serialized ObservationMetada |bKey|
// and returns the persistent store's unique |rowKey|, the random identifier
// |randStr| associated with this rowKey and an error, if any.
// PKey consists of two parts: <BKey>_<Random_id>, where:
//   - BKey is the base64 encoding of the serialization of a |om|, which is also
//     used as an index into Persistent store's bucketSizes map.
//   - Random_id is the base64 encoding of a random 64-bit unsigned integer
//     generated using |SecureRandom|. This random id is also used for
//     shuffling the entries written to leveldb persistent store by the
//     underlying leveldb sort process.
// Panics if input |ObservationMetadata| is nil.
func NewRowKey(bKey string) (rowKey []byte, randStr string, err error) {
	if bKey == "" {
		panic("bKey is empty")
	}

	// Append a random identifier to the ObservatioNmetadata key for performing
	// shuffling. Leveldb uses this random value to sort the keys as it saves the
	// entries in the backend db file, thereby shuffling the new entries as they
	// come in.
	randGenerator := &randutil.SecureRandom{}
	randID, err := randGenerator.RandomUint63(math.MaxInt64)
	if err != nil {
		return []byte(""), "", err
	}

	buf := new(bytes.Buffer)
	if err = binary.Write(buf, binary.LittleEndian, randID); err != nil {
		return []byte(""), "", err
	}
	randStr = base64.StdEncoding.EncodeToString(buf.Bytes())
	rowKey = []byte(makeupRowKey(bKey, randStr))
	return
}

// RowKeyFromMetadata takes an ObservationMetadata |om| and the corresponding
// ObservationVal's identifier |id| and returns the |rowKey| that uniquely
// identifies one observation record in the leveldb persistent store.
func RowKeyFromMetadata(om *shufflerpb.ObservationMetadata, id string) (rowKey string, err error) {
	if om == nil {
		panic("Metadata is nil")
	}
	if id == "" {
		panic("Id is empty")
	}

	bKey, err := BKey(om)
	if err != nil {
		return "", err
	}

	rowKey = makeupRowKey(bKey, id)
	return
}

// BKey returns |bKey| the key for bucketSizes map in the leveldb persistent
// store. BKey is a base64 encoded string derived from marshalled
// |ObservationMetadata|. Panics if input |ObservationMetadata| is nil.
func BKey(om *shufflerpb.ObservationMetadata) (bKey string, err error) {
	if om == nil {
		panic("Metadata is nil")
	}
	omBytes, err := proto.Marshal(om)
	if err != nil {
		return "", err
	}
	bKey = base64.StdEncoding.EncodeToString(omBytes)
	return
}

// UnmarshalBKey decodes the value of |bKey| to the corresponding
// ObservationMetadata |om| or an error. Panics if input |bKey| is empty.
func UnmarshalBKey(bKey string) (om *shufflerpb.ObservationMetadata, err error) {
	if bKey == "" {
		panic("bKey is empty")
	}

	decodedBKey, err := base64.StdEncoding.DecodeString(bKey)
	if err != nil {
		return nil, err
	}
	om = &shufflerpb.ObservationMetadata{}
	if err := proto.Unmarshal(decodedBKey, om); err != nil {
		return nil, err
	}
	return
}

// ExtractBKey returns |bKey| the base64 encoded key prefix from the given
// |pKey|. Panics if input |pKey| is empty and returns an error if pKey is
// corrupted.
func ExtractBKey(pKey string) (bKey string, err error) {
	if pKey == "" {
		panic("pKey is empty")
	}

	index := strings.LastIndex(pKey, "_")
	if index == -1 {
		return "", fmt.Errorf("pKey is invalid: %v", pKey)
	}
	return pKey[0:index], nil
}

// makeupRowKey generates a new row key by joining |bKey| and a unique random
// identifier |idStr| using the "_" delimiter.
func makeupRowKey(bKey string, idStr string) string {
	if bKey == "" {
		panic("bKey is empty")
	}
	if idStr == "" {
		panic("Id is empty")
	}
	return strings.Join([]string{bKey, idStr}, "_")
}
