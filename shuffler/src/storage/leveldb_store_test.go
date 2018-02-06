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

// Unit tests for testing both MemStore and PersistentStore interfaces.

package storage

import (
	"cobalt"
	"testing"

	"github.com/golang/protobuf/proto"
)

// makeLevelDBTestStore creates leveldb |TestStore|.
func makeLevelDBTestStore(t *testing.T) *LevelDBStore {
	leveldbStore, err := NewLevelDBStore("/tmp/shuffler_db")
	if err != nil {
		t.Fatalf("Failed to create a persistent store instance: %v", err)
	}
	return leveldbStore
}

func TestAddGetAndDeleteObservationsForLevelDBStore(t *testing.T) {
	s := makeLevelDBTestStore(t)
	doTestAddGetAndDeleteObservations(t, s)
	ResetStoreForTesting(s, true)
}

func TestShuffleObservationsForLevelDBStore(t *testing.T) {
	s := makeLevelDBTestStore(t)
	doTestShuffle(t, s)
	ResetStoreForTesting(s, true)
}

func TestLevelDBInitialization(t *testing.T) {
	s1 := makeLevelDBTestStore(t)

	// Add one big single ObservationBatch
	const numMsgs = 100
	const arrivalDayIndex = 10
	om := NewObservationMetaData(501)
	batch := NewObservationBatchForMetadata(om, numMsgs)
	if err := s1.AddAllObservations([]*cobalt.ObservationBatch{batch},
		arrivalDayIndex); err != nil {
		t.Errorf("AddAllObservations: got error %v, expected success", err)
	}

	keys, err := s1.GetKeys()
	if err != nil {
		t.Errorf("got error [%v] in fetching keys: %v", err, keys)
	}

	// close existing db handle and clear only in-memory state, but do not destroy
	// the persistent DB state
	ResetStoreForTesting(s1, false)

	// check to see if initialization succeeds reading from the existing DB.
	s2 := makeLevelDBTestStore(t)

	// tests if the bucket keys get pre-populated on creation of new store
	// instances by verifying that the store is no longer empty.
	CheckKeys(t, s2, keys)
	ResetStoreForTesting(s2, true)
}

func TestLevelDBStoreIterator(t *testing.T) {
	s := makeLevelDBTestStore(t)
	defer ResetStoreForTesting(s, true)

	// add observations for different metrics
	const numBatches = 10
	const arrivalDayIndex = 16
	batches := MakeObservationBatches(numBatches)
	if err := s.AddAllObservations(batches, arrivalDayIndex); err != nil {
		t.Errorf("AddAllObservations: got error %v, expected success", err)
	}

	// iterate through each metadata bucket and verify the contents
	for _, batch := range batches {
		om := batch.GetMetaData()
		iter, err := s.GetObservations(om)
		if err != nil {
			t.Errorf("GetObservations: got error %v for metadata [%v]", err, om)
		}

		encMsgList := batch.GetEncryptedObservation()
		gotObVals := CheckIterator(t, iter)
		if gotObVals == nil && len(encMsgList) != 0 {
			t.Errorf("GetObservations() call failed for key: [%v]", om)
			return
		}

		gotEMsgSet := make(map[string]bool, len(gotObVals))
		for _, obVal := range gotObVals {
			gotEMsgSet[proto.CompactTextString(obVal.EncryptedObservation)] = true
		}

		for _, eMsg := range encMsgList {
			_, ok := gotEMsgSet[proto.CompactTextString(eMsg)]
			if !ok {
				t.Errorf("got [%v], expected encrypted message [%v] for metadata [%v] to be present in the resultset", gotEMsgSet, eMsg, om)
				return
			}
		}
	}
}
