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

// MemStore tests.

package storage

import (
	"reflect"
	"sync"
	"testing"

	"cobalt"
	"shuffler"
)

func TestAddGetAndDeleteObservationsForMemStore(t *testing.T) {
	s := NewMemStore()
	doTestAddGetAndDeleteObservations(t, s)
	ResetStoreForTesting(s, true)
}

func TestShuffleObservationsForMemStore(t *testing.T) {
	s := NewMemStore()
	doTestShuffle(t, s)
	ResetStoreForTesting(s, true)
}

// TestShuffle is an unit test on shuffle() method.
func TestShuffle(t *testing.T) {
	num := 10
	// Create the input test ObservationVals.
	testObVals := make([][]*shuffler.ObservationVal, 2)
	// empty list
	testObVals[0] = append(testObVals[0], &shuffler.ObservationVal{})
	// list with num vals
	testObVals[1] = MakeRandomObservationVals(num)

	for _, testObVal := range testObVals {
		shuffledObVal := shuffle(testObVal)

		// Check that basic shuffling occurred.
		if reflect.DeepEqual(shuffledObVal, testObVal) {
			// Skip empty lists
			if len(testObVal) >= 1 && testObVal[0].EncryptedObservation != nil {
				t.Error("error in shuffling observations")
			}
		}
	}
}

// TestMemStoreConcurrency tests that the MemStore correctly handles multiple
// goroutines accessing the same DB instance.
func TestMemStoreConcurrency(t *testing.T) {
	store := NewMemStore()

	// Launch 50 goroutines to simulate multiple instances trying to insert
	// concurrently.
	var wg sync.WaitGroup
	for i := 1; i <= 50; i++ {
		wg.Add(1)
		go func(store *MemStore, index int, t *testing.T) {
			defer wg.Done()
			const arrivalDayIndex = 10

			om := NewObservationMetaData(index)
			batch := NewObservationBatchForMetadata(om, index /*numMsgs*/)

			if err := store.AddAllObservations([]*cobalt.ObservationBatch{batch},
				arrivalDayIndex); err != nil {
				t.Errorf("AddAllObservations: got error %v, expected success", err)
			}
		}(store, i, t)
	}
	wg.Wait()

	for i := 1; i <= 50; i++ {
		wg.Add(1)
		go func(store *MemStore, index int, t *testing.T) {
			defer wg.Done()
			om := NewObservationMetaData(index)
			CheckNumObservations(t, store, om, index)
		}(store, i, t)
	}
	wg.Wait()

	// Verify count of saved keys after concurrent deletion for metric#6
	var keys []*cobalt.ObservationMetadata
	var err error
	if keys, err = store.GetKeys(); err != nil {
		t.Errorf("GetKeys() error: [%v]", err)
		return
	}
	wg.Wait()

	// Delete 5 keys concurrently
	deleteAndVerify := func(store *MemStore, index int, t *testing.T) {
		om := NewObservationMetaData(index)
		iter, err := store.GetObservations(om)
		if err != nil {
			t.Errorf("GetObservations: got error [%v] for metadata [%v]", err, om)
		}
		var vals []*shuffler.ObservationVal
		for iter.Next() {
			obVal, iErr := iter.Get()
			if iErr != nil {
				t.Errorf("got error on iter.Get() for key [%v]: %v", om, err)
			}
			vals = append(vals, obVal)
		}
		if err := iter.Release(); err != nil {
			t.Errorf("got error on iter.Release() for metadata [%v]: %v", om, err)
		}

		// delete all values for this metric
		if err := store.DeleteValues(om, vals); err != nil {
			t.Errorf("DeleteValues: got error [%v] for metadata [%v]", err, om)
		}

		// delete |om| from keys
		for i := 0; i < len(keys); i++ {
			if keys[i].CustomerId == om.CustomerId {
				keys[i] = keys[len(keys)-1]
				keys[len(keys)-1] = nil
				keys = keys[:len(keys)-1]
			}
		}
	}

	for i := 5; i < 10; i++ {
		wg.Add(1)
		go func(store *MemStore, index int, t *testing.T) {
			defer wg.Done()
			deleteAndVerify(store, index, t)
		}(store, i, t)
	}
	wg.Wait()

	// Verify count of saved keys after concurrent deletion for metric#6
	om := NewObservationMetaData(6)
	if _, err := store.GetNumObservations(om); err == nil {
		t.Errorf("GetNumObservations: expected [Key not found] error for metadata [%v]", om)
	}

	// Verify total keys after successful deletion of 5 metrics
	CheckKeys(t, store, keys)
}

func TestMemStoreIterator(t *testing.T) {
	testObVals := MakeRandomObservationVals(50)

	// test against a list with only one entry and multiple entries.
	for _, obVals := range [][]*shuffler.ObservationVal{testObVals[:1], testObVals} {
		gotObVals := CheckIterator(t, NewMemStoreIterator(obVals))
		if !reflect.DeepEqual(obVals, gotObVals) {
			t.Errorf("CheckIterator: got [%v], expected [%v] ObservationVals", len(gotObVals), len(obVals))
		}
	}
}
