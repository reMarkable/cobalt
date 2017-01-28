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
	"reflect"
	"testing"
	"time"

	shufflerpb "cobalt"
)

// TestGetDayIndexUtc tests the utility function that computes day index for the
// stored observation.
func TestGetDayIndexUtc(t *testing.T) {
	// test against difference in days
	for i := 1; i <= 32; i++ {
		testTime := time.Date(1970, time.January, i, 0, 0, 0, 0, time.UTC)
		di := GetDayIndexUtc(testTime)
		if int(di) != i-1 {
			t.Errorf("got day index [%d], want day index [%d]", di, i-1)
		}
	}

	// test against difference in hours
	for i := 1; i <= 25; i++ {
		testTime := time.Date(1970, time.January, 3, i, 0, 0, 0, time.UTC)
		di := GetDayIndexUtc(testTime)
		if i < 24 && di != uint32(2) {
			t.Errorf("got day index [%d] for i:%d, want day index [2]", di, i)
		}
		if i > 24 && di != uint32(3) { // spills into next day
			t.Errorf("got day index [%d] for i:%d, want day index [3]", di, i)
		}
	}

	// test against difference in mins
	for i := 1; i <= 128; i++ {
		testTime := time.Date(1970, time.January, 3, 0, i, 0, 0, time.UTC)
		di := GetDayIndexUtc(testTime)
		if di != uint32(2) {
			t.Errorf("got day index [%d] for time:%v, want day index [%d]", di, testTime, 2)
		}
	}

	// test against difference in secs
	for i := 0; i <= 86450; i += 3600 {
		testTime := time.Date(1970, time.January, 3, 0, 0, i, 0, time.UTC)
		di := GetDayIndexUtc(testTime)
		if i < 86400 && di != uint32(2) {
			t.Errorf("got day index [%d] for time:%v, want day index [%d]", di, testTime, 2)
		} else if i >= 86400 && di != uint32(3) { // spills into next day
			t.Errorf("got day index [%d] for time:%v, want day index [%d]", di, testTime, 3)
		}
	}

	// test against leap years. In this example, there are 2 leap years in between
	// 1970 and 1980 which accounts for 2 extra days in dayIndex.
	testTime := time.Date(1980, time.January, 1, 0, 0, 0, 0, time.UTC)
	di := GetDayIndexUtc(testTime)
	if di != 3652 {
		t.Errorf("got day index [%d] for time:%v, want day index [3652]", di, testTime)
	}
}

// TestNewObservationVal verifies the constructor that builds |ObservationVal|.
func TestNewObservationVal(t *testing.T) {
	eMsg := &shufflerpb.EncryptedMessage{
		Scheme:     shufflerpb.EncryptedMessage_NONE,
		PubKey:     "analyzer_pub_key",
		Ciphertext: []byte("ciphertext"),
	}
	testDayIndex := uint32(17201)
	val := NewObservationVal(eMsg, "test", testDayIndex)
	if val == nil {
		t.Error("got empty ObservationVal")
	}

	// test arrival day index
	if val.ArrivalDayIndex != testDayIndex {
		t.Errorf("got day_index [%d], want day_index [%d]", val.ArrivalDayIndex, testDayIndex)
	}

	// test encrypted message
	if eMsg != val.EncryptedObservation {
		t.Errorf("got encrypted_message [%v], want encrypted_message [%v]", val.EncryptedObservation, eMsg)
	}

	// test id
	if val.Id != "test" {
		t.Errorf("got id [%v], want id [%v]", val.Id, "test")
	}
}

// doTestAddGetAndDeleteObservations tests the Store methods
// AddAllObservations, GetObservations, GetNumObservations, GetKeys and
// DeleteValues.
func doTestAddGetAndDeleteObservations(t *testing.T, store Store) {
	const numBatches = 10
	const arrivalDayIndex = 16

	// add observations for different metrics
	batches := MakeObservationBatches(numBatches)
	if err := store.AddAllObservations(batches, arrivalDayIndex); err != nil {
		t.Errorf("AddAllObservations: got error %v, expected success", err)
	}

	var keys []*shufflerpb.ObservationMetadata

	// verify each metadata bucket
	for _, batch := range batches {
		om := batch.GetMetaData()
		keys = append(keys, om)

		encMsgList := batch.GetEncryptedObservation()
		CheckNumObservations(t, store, om, len(encMsgList))

		CheckGetObservations(t, store, om, encMsgList)
	}

	// verify all keys
	CheckKeys(t, store, keys)

	// verify delete for metadata with MetricId=7
	om := batches[7].GetMetaData()

	// Retrieve stored observation contents before deletion for metric 8
	vals, err := store.GetObservations(om)
	if err != nil {
		t.Errorf("GetObservations: got error [%v], expected valid observations", err)
	}

	// call delete for half the observations
	deleteObVals := vals[0 : len(vals)/2]
	if err := store.DeleteValues(om, deleteObVals); err != nil {
		t.Errorf("DeleteValues: got error %v, expected successful deletion of obVals for metadata [%v]", err, om)
	}

	numValsAfterDeletion := len(vals) - len(deleteObVals)

	var undeletedEMsgs []*shufflerpb.EncryptedMessage
	for _, val := range vals[len(vals)/2:] {
		undeletedEMsgs = append(undeletedEMsgs, val.EncryptedObservation)
	}

	// Verify deleted and stored observation contents after DeleteValues() call
	CheckNumObservations(t, store, om, numValsAfterDeletion)
	CheckDeleteObservations(t, store, om, numValsAfterDeletion, deleteObVals)
	CheckGetObservations(t, store, om, undeletedEMsgs)
}

// doTestShuffle tests that the store returns shuffled observations for each
// key.
func doTestShuffle(t *testing.T, store Store) {
	const numMsgs = 100
	const arrivalDayIndex = 10

	// Add one big single ObservationBatch
	om := NewObservationMetaData(501)
	batch := NewObservationBatchForMetadata(om, numMsgs)
	if err := store.AddAllObservations([]*shufflerpb.ObservationBatch{batch},
		arrivalDayIndex); err != nil {
		t.Errorf("AddAllObservations: got error %v, expected success", err)
	}

	shuffledObVals := CheckObservations(t, store, om, numMsgs)
	if len(shuffledObVals) == 0 {
		t.Errorf("GetObservations() call failed for key: [%v]", om)
		return
	}

	// extract emsgs from shuffledObVals
	var shuffledEMsgs []*shufflerpb.EncryptedMessage
	for _, obVal := range shuffledObVals {
		shuffledEMsgs = append(shuffledEMsgs, obVal.EncryptedObservation)
	}

	// extract emsgs from original input batch
	inputEMsgs := batch.GetEncryptedObservation()

	shuffledCount := 0
	for i := 0; i < numMsgs; i++ {
		if !reflect.DeepEqual(inputEMsgs[i], shuffledEMsgs[i]) {
			// observation got shuffled
			shuffledCount++
		}
	}

	// Check that basic shuffling occurred using some rough threshold.
	if shuffledCount > 10 {
		t.Logf("got [%v] shuffled observations out of [%d] total observations", shuffledCount, numMsgs)
	}
}
