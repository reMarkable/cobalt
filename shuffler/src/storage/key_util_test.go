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
	shufflerpb "cobalt"
	"encoding/base64"
	"encoding/binary"
	"reflect"
	"strings"
	"testing"

	"github.com/golang/protobuf/proto"
)

// getTestMetadata constructs fake observation metadata for testing.
func getTestMetadata(customerID int, projectID int, metricID int, dayIndex int) *shufflerpb.ObservationMetadata {
	return &shufflerpb.ObservationMetadata{
		CustomerId: uint32(customerID),
		ProjectId:  uint32(projectID),
		MetricId:   uint32(metricID),
		DayIndex:   uint32(dayIndex),
	}
}

func verify(t *testing.T, pKeyBytes []byte, id string) {
	pKey := string(pKeyBytes)
	index := strings.LastIndex(pKey, "_")
	part1 := pKey[0:index]
	part2 := pKey[index+1 : len(pKey)]
	if _, err := UnmarshalBKey(part1); err != nil {
		t.Errorf("got error [%v]", err)
	}

	if id != part2 {
		t.Errorf("got rowkey index [%v], want rowkey index [%v]", id, part2)
	}

	if randIDbytes, err := base64.StdEncoding.DecodeString(string(part2)); err != nil {
		t.Errorf("got error [%v], want random id", err)
	} else {
		buf := bytes.NewReader(randIDbytes)
		var randID uint64
		if err := binary.Read(buf, binary.LittleEndian, &randID); err != nil {
			t.Errorf("got error in parsing rand id: %v", err)
		}
	}
}

func TestPKey(t *testing.T) {
	om := getTestMetadata(11, 12, 13, 14)
	bKey, err := BKey(om)
	if err != nil {
		t.Errorf("got error [%v], want bKey for metadata [%v]", err, om)
	}

	key, id, _ := NewRowKey(bKey)
	verify(t, key, id)
}

func TestBKey(t *testing.T) {
	om := getTestMetadata(1, 2, 3, 4)
	bKey, err := BKey(om)
	if err != nil {
		t.Errorf("got error [%v], want bKey for metadata [%v]", err, om)
	}
	decodedBKey, err := base64.StdEncoding.DecodeString(bKey)
	if err != nil {
		t.Errorf("got error [%v], want [%v]", err, om)
	}
	got := &shufflerpb.ObservationMetadata{}
	if err := proto.Unmarshal(decodedBKey, got); err != nil {
		t.Errorf("got error [%v], want [%v]", err, om)
	} else if !reflect.DeepEqual(got, om) {
		t.Errorf("got [%v], want [%v]", got, om)
	}
}

func TestUnmarshalBKey(t *testing.T) {
	om := getTestMetadata(551, 12, 343, 890)
	bKey, err := BKey(om)
	if err != nil {
		t.Errorf("got error [%v], want bKey for metadata [%v]", err, om)
	}
	if got, err := UnmarshalBKey(bKey); err != nil {
		t.Errorf("got error [%v], want unmarshalled proto", err)
	} else if !reflect.DeepEqual(om, got) {
		t.Errorf("got [%v], want [%v]", got, om)
	}
}

func TestExtractBKey(t *testing.T) {
	om := getTestMetadata(11, 12, 13, 14)
	bKey1, err := BKey(om)
	if err != nil {
		t.Errorf("got error [%v], want bKey for metadata [%v]", err, om)
	}
	key, _, _ := NewRowKey(bKey1)
	bKey2, err := ExtractBKey(string(key))
	if err != nil {
		t.Errorf("got [%v] in extractBKey()", err)
	}

	// valid bKey
	if bKey1 != bKey2 {
		t.Errorf("got bKey [%v], want bKey [%v]", bKey1, bKey2)
	}
	if _, err = UnmarshalBKey(bKey2); err != nil {
		t.Errorf("got [%v], want no error ", err)
	}

	// invalid pKey, no delimiter
	if bKey, err := ExtractBKey("invalidpkey"); err == nil {
		t.Errorf("got [%v], want error [%v]", bKey, err)
	}

	// any pKey string with delimiter is considered valid
	if bKey, _ := ExtractBKey("invalid_pkey"); bKey == "" {
		t.Errorf("got [%v], want [%v]", bKey, "invalid")
	}
}
