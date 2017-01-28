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
	shufflerpb "cobalt"
	"testing"
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
	if err := s1.AddAllObservations([]*shufflerpb.ObservationBatch{batch},
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
