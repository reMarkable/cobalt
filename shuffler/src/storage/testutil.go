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

// Testutil.go implements common functionality used in testing both MemStore and
// PersistentStore interfaces.

package storage

import (
	"strconv"
	"testing"
	"util"

	"github.com/golang/protobuf/proto"

	"cobalt"
	"shuffler"
)

var r = util.NewDeterministicRandom(int64(1))

// NewObservationMetaData constructs fake observation metadata for testing.
func NewObservationMetaData(testID int) *cobalt.ObservationMetadata {
	id := uint32(testID)
	return &cobalt.ObservationMetadata{
		CustomerId: id,
		ProjectId:  id,
		MetricId:   id,
		DayIndex:   id,
	}
}

// MakeRandomObservationVals constructs a list of fake |ObservationVal|s for
// testing.
func MakeRandomObservationVals(numMsgs int) []*shuffler.ObservationVal {
	var vals []*shuffler.ObservationVal
	eMsgList := MakeRandomEncryptedMsgs(numMsgs)
	for i := 0; i < numMsgs; i++ {
		vals = append(vals, NewObservationVal(eMsgList[i], strconv.Itoa(i), 999))
	}

	return vals
}

// MakeRandomEncryptedMsgs returns a list of random |EncryptedMessages| using
// the default Scheme and Analyzer's public key hash.
func MakeRandomEncryptedMsgs(numMsgs int) []*cobalt.EncryptedMessage {
	var eMsgList []*cobalt.EncryptedMessage
	for i := 0; i < numMsgs; i++ {
		bytes, _ := r.RandomBytes(10)
		eMsgList = append(eMsgList, &cobalt.EncryptedMessage{
			Ciphertext: bytes,
		})
	}
	return eMsgList
}

// NewObservationBatchForMetadata creates a random |ObservationBatch| for the
// given metadata |om|.
func NewObservationBatchForMetadata(om *cobalt.ObservationMetadata, numMsgs int) *cobalt.ObservationBatch {
	return &cobalt.ObservationBatch{
		MetaData:             om,
		EncryptedObservation: MakeRandomEncryptedMsgs(numMsgs),
	}
}

// MakeObservationBatches generates a set of ObservationBatches of size
// |numBatches|. For each i, batch i will have an ObservationMetadata that uses
// i for all IDs and will contain i encrypted observations.
func MakeObservationBatches(numBatches int) []*cobalt.ObservationBatch {
	var batches []*cobalt.ObservationBatch
	for i := 1; i <= numBatches; i++ {
		batches = append(batches, NewObservationBatchForMetadata(NewObservationMetaData(i), i))
	}
	return batches
}

// CheckKeys tests if the total count of ObservationMetadata keys saved in
// store is equal to |expectedNumKeys| and the contents of the keys match with
// the given |expectedKeys| list.
func CheckKeys(t *testing.T, store Store, expectedKeys []*cobalt.ObservationMetadata) {
	if store == nil {
		panic("store is nil")
	}
	gotKeys, err := store.GetKeys()
	if err != nil {
		t.Errorf("GetKeys: got keys [%v] with error: %v, expected empty list", gotKeys, err)
	}

	if len(gotKeys) != len(expectedKeys) {
		t.Errorf("GetKeys: got [%d] keys, expected [%d] keys", gotKeys, len(expectedKeys))
	}

	gotKeysSet := make(map[string]bool, len(gotKeys))
	for _, key := range gotKeys {
		gotKeysSet[proto.CompactTextString(key)] = true
	}

	for _, key := range expectedKeys {
		_, ok := gotKeysSet[proto.CompactTextString(key)]
		if !ok {
			t.Errorf("got keys [%v], expected key [%v] to be present in the resultset", gotKeysSet, key)
			return
		}
	}
}

// CheckNumObservations tests if the total count of observations returned by
// GetNumObservations() call for a given ObservationMetadata |om| key is equal
// to |expectedNumObs|.
func CheckNumObservations(t *testing.T, store Store, om *cobalt.ObservationMetadata, expectedNumObs int) {
	if store == nil {
		panic("store is nil")
	}
	if om == nil {
		panic("Metadata is nil")
	}

	if obValsLen, err := store.GetNumObservations(om); err != nil && expectedNumObs != 0 {
		t.Errorf("GetNumObservations: got error [%v] for metadata [%v]", err, om)
	} else if obValsLen != expectedNumObs {
		t.Errorf("GetNumObservations: got [%d] ObservationVals, expected [%d] ObservationVals per metadata [%v]", obValsLen, expectedNumObs, om)
	}
}

// CheckObservations tests if the total count of observations returned by
// GetObservations() for a given ObservationMetadata |om| key is equal to
// |expectedNumObs|, and returns the fetched list of |ObservationVal|s.
func CheckObservations(t *testing.T, store Store, om *cobalt.ObservationMetadata, expectedNumObs int) []*shuffler.ObservationVal {
	if store == nil {
		panic("store is nil")
	}
	if om == nil {
		panic("Metadata is nil")
	}

	iter, err := store.GetObservations(om)
	if err != nil {
		t.Errorf("GetObservations: got error %v for metadata [%v]", err, om)
	}

	if iter == nil {
		t.Errorf("GetObservations: got empty iterator for metadata [%v]", om)
	}

	var gotObVals []*shuffler.ObservationVal
	for iter.Next() {
		obVal, iErr := iter.Get()
		if iErr != nil {
			t.Errorf("got error on iter.Get() for key [%v]: %v", om, err)
		}
		gotObVals = append(gotObVals, obVal)
	}
	if err := iter.Release(); err != nil {
		t.Errorf("got error on iter.Release() for metadata [%v]: %v", om, err)
	}

	if len(gotObVals) != expectedNumObs {
		t.Errorf("GetObservations: got [%d] observations, expected [%d] observations for metadata [%v]", len(gotObVals), expectedNumObs, om)
	}

	return gotObVals
}

// CheckGetObservations tests if the observations fetched from store for a given
// ObservationMetadata |om| key are valid. Observations are deemed valid if and
// only if:
// - The total count of observations returned by GetObservations() is equal to
//   length of |expectedEncMsgs|, and
// - The contents of stored |EncryptedMessages|s match with the given
//   |expectedEncMsgs| list.
func CheckGetObservations(t *testing.T, store Store, om *cobalt.ObservationMetadata, expectedEncMsgs []*cobalt.EncryptedMessage) {
	gotObVals := CheckObservations(t, store, om, len(expectedEncMsgs))
	if gotObVals == nil && len(expectedEncMsgs) != 0 {
		t.Errorf("GetObservations() call failed for key: [%v]", om)
		return
	}

	gotEMsgSet := make(map[string]bool, len(gotObVals))
	for _, obVal := range gotObVals {
		gotEMsgSet[proto.CompactTextString(obVal.EncryptedObservation)] = true
	}

	for _, eMsg := range expectedEncMsgs {
		_, ok := gotEMsgSet[proto.CompactTextString(eMsg)]
		if !ok {
			t.Errorf("got [%v], expected encrypted message [%v] for metadata [%v] to be present in the resultset", gotEMsgSet, eMsg, om)
			return
		}
	}
}

// CheckDeleteObservations tests if the observations fetched from store for a
// given ObservationMetadata |om| key are valid after a successful
// DeleteValues() call, if and only if:
// - The total count of observations returned by GetObservations() is equal to
//   |expectedNumObs|, and
// - The fetched observations doesn't contain any ObservationVal from the
//   |expectedDeleteVals| list.
func CheckDeleteObservations(t *testing.T, store Store, om *cobalt.ObservationMetadata, expectedNumObs int, expectedDeleteVals []*shuffler.ObservationVal) {
	gotObVals := CheckObservations(t, store, om, expectedNumObs)
	if gotObVals == nil && expectedNumObs != 0 {
		t.Errorf("GetObservations() call failed for key: [%v]", om)
		return
	}

	for _, obVal := range expectedDeleteVals {
		for _, gotObVal := range gotObVals {
			if obVal.Id == gotObVal.Id {
				t.Errorf("Observation val [%v] still exists after delete() call for metadata [%v] with vals: [%v]", obVal, om, gotObVals)
				return
			}
		}
	}
}

// CheckIterator tests if the given |iter| is valid and then fetches all the
// ObservationVals using |iter| until the iterator gets exhausted.
func CheckIterator(t *testing.T, iter Iterator) []*shuffler.ObservationVal {
	var gotObVals []*shuffler.ObservationVal
	if iter == nil {
		return gotObVals
	}

	for iter.Next() {
		val, err := iter.Get()
		if err != nil {
			t.Errorf("iter.Get() returned error: %v", err)
		}
		gotObVals = append(gotObVals, val)
	}

	err := iter.Release()
	if err != nil {
		t.Errorf("got error while releasing iterator: %v", err)
	}

	if iter.Next() {
		t.Errorf("iterator must be empty after release()")
	}

	return gotObVals
}

// ResetStoreForTesting clears any in-memory caches, and deletes all data
// in the store if |destroy| is true. For MemStore, data gets destroyed
// irrespective of the |destroy| value.
func ResetStoreForTesting(store Store, destroy bool) {
	switch s := store.(type) {
	case *MemStore:
		s.Reset()
	case *LevelDBStore:
		s.Reset(destroy)
	default:
		panic("unsupported store type")
	}
}
