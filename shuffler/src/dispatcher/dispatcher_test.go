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

package dispatcher

import (
	"fmt"
	"reflect"
	"testing"
	"time"

	"cobalt"
	"shuffler"
	"storage"
)

// This is a fake Analyzer transport client that just caches the Observations
// in the order they are received. This lets us verify the output of the
// dispatcher.
type fakeAnalyzerTransport struct {
	obBatch []*cobalt.ObservationBatch
	numSent int
}

func (a *fakeAnalyzerTransport) send(obBatch *cobalt.ObservationBatch) error {
	if obBatch == nil {
		return nil
	}

	a.numSent++
	a.obBatch = append(a.obBatch, obBatch)
	return nil
}

func (a *fakeAnalyzerTransport) close() {
	// do nothing
}

func (a *fakeAnalyzerTransport) reconnect() {
	// do nothing
}

// makeTestStore returns a sample test store with |numObservations| for a single
// ObservationMetadata key and its generated |obVals| or an error.
//
// The observations are divided in such a way that first 1/4th of the
// observations are generated for the |currentDayIndex|, next 1/4th are
// generated for |currentDayIndex - 1| and so on spanning a range of 4
// dayIndexes.
//
// The test store created is an in-memory store if |useMemStore| is true.
// Otherwise, a LevelDB persistent store is used.
func makeTestStore(numObservations int, currentDayIndex uint32, useMemStore bool) (storage.Store, *cobalt.ObservationMetadata, []*shuffler.ObservationVal, error) {
	var store storage.Store
	var err error
	if useMemStore {
		store = storage.NewMemStore()
	} else {
		if store, err = storage.NewLevelDBStore("/tmp/dispatcher_db"); err != nil {
			return nil, nil, nil, err
		}
	}

	om := storage.NewObservationMetaData(22)
	dayIndexRange := []uint32{
		currentDayIndex - 1,
		currentDayIndex - 2,
		currentDayIndex - 3,
		currentDayIndex - 4}
	for _, di := range dayIndexRange {
		batch := &cobalt.ObservationBatch{
			MetaData:             om,
			EncryptedObservation: storage.MakeRandomEncryptedMsgs(numObservations / 4),
		}

		if err = store.AddAllObservations([]*cobalt.ObservationBatch{batch}, di); err != nil {
			return nil, nil, nil, err
		}
	}

	// Get all observations in one big chunk
	iter, err := store.GetObservations(om)
	if err != nil {
		return nil, nil, nil, err
	}

	if iter == nil {
		return nil, nil, nil, fmt.Errorf("GetObservations: got empty iterator for metadata [%v]", om)
	}

	var obVals []*shuffler.ObservationVal
	for iter.Next() {
		obVal, iErr := iter.Get()
		if iErr != nil {
			return nil, nil, nil, iErr
		}
		obVals = append(obVals, obVal)
	}
	if err := iter.Release(); err != nil {
		return nil, nil, nil, err
	}

	return store, om, obVals, nil
}

// newTestDispatcher generates a new dispatcher using test configuration for
// the given |batchSize|, |threshold|, |frequencyInHours| and |store| values.
//
// Panics if |store| is not set.
func newTestDispatcher(store storage.Store, batchSize int, threshold int) *Dispatcher {
	if store == nil {
		panic("store is nil")
	}

	// testconfig for immediate dispatch for smaller batches
	testConfig := &shuffler.ShufflerConfig{}
	testConfig.GlobalConfig = &shuffler.Policy{
		FrequencyInHours: 0,
		PObservationDrop: 0.0,
		Threshold:        uint32(threshold),
		AnalyzerUrl:      "localhost",
		DisposalAgeDays:  100,
	}

	analyzerTransport := fakeAnalyzerTransport{numSent: 0}
	return &Dispatcher{
		store:             store,
		config:            testConfig,
		batchSize:         batchSize,
		analyzerTransport: &analyzerTransport,
		lastDispatchTime:  time.Now(),
	}
}

// getAnalyzerTransport returns analyzerTransport handle from the given
// Dispatcher |d|.
func getAnalyzerTransport(d *Dispatcher) *fakeAnalyzerTransport {
	if d == nil {
		panic("dispatcher is nil")
	}

	switch a := d.analyzerTransport.(type) {
	case *fakeAnalyzerTransport:
		return a
	default:
		panic("unsupported store type")
	}
}

// doTestDeleteOldObservations tests deleteOldObservations() method.
func doTestDeleteOldObservations(t *testing.T, useMemStore bool) {
	const num = 4
	const currentDayIndex = 10

	store, key, _, err := makeTestStore(num, currentDayIndex, useMemStore)
	if err != nil {
		t.Fatalf("got error [%v] in test store setup", err)
	}

	storage.CheckNumObservations(t, store, key, num)

	// make test dispatcher by setting threshold and frequency to "0" and
	// batchsize to max |num| for immediate dispatch.
	d := newTestDispatcher(store, num, 0)

	// Dispose off any older messages that have a dayIndex less than "4".
	disposalAgeInDays := uint32(4)
	err = d.deleteOldObservations(key, currentDayIndex, disposalAgeInDays)
	if err != nil {
		t.Errorf("Expected successful update, got error [%v]", err)
		return
	}
	// none of the stored observations gets deleted
	storage.CheckNumObservations(t, store, key, num)
	obValsAfterDeletion := storage.CheckObservations(t, store, key, num)
	for _, obVal := range obValsAfterDeletion {
		if obVal.ArrivalDayIndex <= disposalAgeInDays {
			t.Errorf("Expected ObVal with dayIndex [%d] to be deleted", obVal.Id)
			return
		}
	}

	// Dispose off any older messages that have a dayIndex less than "2".
	disposalAgeInDays = uint32(2)
	err = d.deleteOldObservations(key, currentDayIndex, disposalAgeInDays)
	if err != nil {
		t.Errorf("Expected successful update, got error [%v]", err)
		return
	}
	// this list must contain exactly half the observations
	storage.CheckNumObservations(t, store, key, num/2)
	obValsAfterDeletion = storage.CheckObservations(t, store, key, num/2)
	for _, obVal := range obValsAfterDeletion {
		if obVal.ArrivalDayIndex <= disposalAgeInDays {
			t.Errorf("Expected ObVal with dayIndex [%d] to be deleted", obVal.Id)
			return
		}
	}

	// Dispose off all messages by specifying dayIndex "0".
	disposalAgeInDays = uint32(0)
	err = d.deleteOldObservations(key, currentDayIndex, disposalAgeInDays)
	if err != nil {
		t.Errorf("Expected successful update, got error [%v]", err)
		return
	}

	// all the observations for the given key should be deleted
	storage.CheckNumObservations(t, store, key, 0)

	// reset store
	storage.ResetStoreForTesting(d.store, true)
}

// doTestDispatchInBatches tests dispatch() method using varying |batchSize|s.
func doTestDispatchInBatches(t *testing.T, useMemStore bool) {
	const num = 40
	const currentDayIndex = 10

	// Test dispatch for different batch sizes
	for _, batchSize := range []int{1, 10, 20, 40, 100} {
		// Recreate the test store with |num| entries for every run as they get
		// deleted after every successful dispatch attempt.
		store, key, obVals, err := makeTestStore(num, currentDayIndex, useMemStore)
		if err != nil {
			t.Fatalf("got error [%v] in test store setup", err)
		}

		if len(obVals) != num {
			t.Errorf("BatchSize: [%d], got observations [%v], expected [%v]", batchSize, len(obVals), num)
		}

		// run dispatcher by setting threshold and frequency to "0" and different
		// batchsizes.
		d := newTestDispatcher(store, batchSize, 0)
		analyzer := getAnalyzerTransport(d)
		d.dispatch(1 * time.Millisecond)

		// Assert that last timestamp has been modified to the current time.
		now := time.Now()
		if uint32(d.lastDispatchTime.Sub(now).Minutes()) != 0 {
			t.Errorf("got last dispatch time [%v], expected last dispatch time [%v] to be updated to current", d.lastDispatchTime, now)
		}

		// check if all msgs are sent
		if num >= d.batchSize {
			if len(analyzer.obBatch) != num/d.batchSize {
				t.Errorf("BatchSize: [%d], unexpected number of observations dispatched, got [%v] expected [%v]", d.batchSize, len(analyzer.obBatch), num)
			}
		} else if len(analyzer.obBatch) != 1 {
			t.Errorf("BatchSize: [%d], expected all observations to be sent at once, got [%v] expected [1]", d.batchSize, len(analyzer.obBatch))
		}

		// check if the data is sent in batches based on batchSize
		if num >= d.batchSize {
			if analyzer.numSent != num/d.batchSize {
				t.Errorf("BatchSize: [%d], unexpected number of analyzer send calls, got [%d], want [%d]", d.batchSize, analyzer.numSent, num/d.batchSize)
			}
		} else if analyzer.numSent != 1 {
			t.Errorf("BatchSize: [%d], expected all observations to be sent at once, got [%d], want [1]", d.batchSize, analyzer.numSent)
		}

		// check if all the sent msgs are deleted from the Shuffler datastore
		if obValsLen, _ := d.store.GetNumObservations(key); obValsLen != 0 {
			t.Errorf("BatchSize: [%d], got [%d] observations, expected [0] observations in the store for meatdata [%v]", d.batchSize, obValsLen, key)
		}

		// reset analyzer
		analyzer.numSent = 0
		analyzer.obBatch = nil

		// reset store
		storage.ResetStoreForTesting(d.store, true)
	}
}

// doTestDispatchInBatches tests dispatch() method using varying |threshold|
// limits.
func doTestDispatchBasedOnThresholds(t *testing.T, useMemStore bool) {
	const num = 40
	const currentDayIndex = 10

	// Test dispatch for different thresholds
	for _, threshold := range []int{1, 10, 20, 40, 80, 100} {
		// Recreate the test store with |num| entries for every run as they get
		// deleted after every successful dispatch attempt.
		store, key, obVals, err := makeTestStore(num, currentDayIndex, useMemStore)
		if err != nil {
			t.Fatalf("got error [%v] in test store setup", err)
		}

		if len(obVals) != num {
			t.Errorf("Threshold: [%d], got observations [%v], expected [%v]", threshold, len(obVals), num)
		}

		// run dispatcher with frequency set to "0" and batchsize set to the max
		// chunk size - "num" for sending all messages at once in one large batch.
		d := newTestDispatcher(store, num, threshold)
		analyzer := getAnalyzerTransport(d)
		d.dispatch(1 * time.Millisecond)

		// Assert that last timestamp has been modified to the current time.
		now := time.Now()
		if uint32(d.lastDispatchTime.Sub(now).Minutes()) != 0 {
			t.Errorf("got last dispatch time [%v], expected last dispatch time [%v] to be updated to current", d.lastDispatchTime, now)
		}

		if num < threshold {
			if analyzer.numSent != 0 {
				t.Errorf("Threshold: [%d], unexpected number of analyzer send calls, got [%d], want [0]", threshold, analyzer.numSent)
			}
		} else {
			// check if all msgs are sent
			if len(analyzer.obBatch) != 1 {
				t.Errorf("Threshold: [%d], unexpected number of observations dispatched, got [%v] expected [1]", threshold, len(analyzer.obBatch))
			}

			// make sure that all sent msgs are deleted from the Shuffler datastore
			if obValsLen, _ := store.GetNumObservations(key); obValsLen != 0 {
				t.Errorf("Threshold: [%d], got [%d] observations, expected [0] observations in the store for meatdata [%v]", threshold, obValsLen, key)
			}
		}

		// reset analyzer
		analyzer.numSent = 0
		analyzer.obBatch = nil

		// reset store
		storage.ResetStoreForTesting(store, true)
	}
}

func TestDeleteOldObservationsForMemStore(t *testing.T) {
	doTestDeleteOldObservations(t, true)
}

func TestDeleteOldObservationsForLevelDBStore(t *testing.T) {
	doTestDeleteOldObservations(t, false)
}

func TestDispatchInBatchesForMemStore(t *testing.T) {
	doTestDispatchInBatches(t, true)
}

func TestDispatchInBatchesForLevelDBStore(t *testing.T) {
	doTestDispatchInBatches(t, false)
}

func TestThresholdBasedDispatchForMemStore(t *testing.T) {
	doTestDispatchBasedOnThresholds(t, true)
}

func TestThresholdBasedDispatchForLevelDBStore(t *testing.T) {
	doTestDispatchBasedOnThresholds(t, false)
}

func TestComputeWaitTime(t *testing.T) {
	// create a test dispatcher with all defaults
	d := newTestDispatcher(storage.NewMemStore(), 1, 0)

	// Case 1
	// lastDispatchTime = 0
	// FrequencyInHours = 0
	// expected result: wait <=0
	// Dispatch frequency set to 0, always dispatch!
	d.lastDispatchTime = time.Time{}
	if waitTime := d.computeWaitTime(time.Now()); waitTime > 0 {
		t.Errorf("waitTime=%v", waitTime)
	}

	// Case 2
	// lastDispatchTime = Now
	// FrequencyInHours = 0
	// expected result: wait <=0
	// Dispatch frequency set to 0, always dispatch!
	d.lastDispatchTime = time.Now()
	if waitTime := d.computeWaitTime(time.Now()); waitTime > 0 {
		t.Errorf("waitTime=%v", waitTime)
	}

	// Case 3
	// lastDispatchTime = 0
	// FrequencyInHours = 24
	// expected result: wait <=0
	d.lastDispatchTime = time.Time{}
	d.config.GlobalConfig.FrequencyInHours = uint32(24)
	if waitTime := d.computeWaitTime(time.Now()); waitTime > 0 {
		t.Errorf("d.lastDispatchTime=%v, waitTime=%v", d.lastDispatchTime, waitTime)
	}

	// Case 4
	// lastDispatchTime = 20 hours ago
	// FrequencyInHours = 24
	// expected result: wait ~ 4 hours
	d.lastDispatchTime = time.Now().Add(time.Duration(-20) * time.Hour)
	waitTime := d.computeWaitTime(time.Now())
	if waitTime < time.Duration(4)*time.Hour-time.Duration(1)*time.Minute {
		t.Errorf("waitTime=%v", waitTime)
	}
	if waitTime > time.Duration(4)*time.Hour+time.Duration(1)*time.Minute {
		t.Errorf("waitTime=%v", waitTime)
	}

	// Case 5
	// lastDispatchTime = 30 hours ago
	// FrequencyInHours = 24
	// expected result: wait <=0
	d.lastDispatchTime = time.Now().Add(time.Duration(-30) * time.Hour)
	waitTime = d.computeWaitTime(time.Now())
	if waitTime > 0 {
		t.Errorf("waitTime=%v", waitTime)
	}
}

func TestMakeBatch(t *testing.T) {
	dayIndex := storage.GetDayIndexUtc(time.Now())
	key := &cobalt.ObservationMetadata{
		CustomerId: uint32(1),
		ProjectId:  uint32(11),
		MetricId:   uint32(111),
		DayIndex:   dayIndex,
	}
	obVals := storage.MakeRandomObservationVals(30)
	iterator := storage.NewMemStoreIterator(obVals)

	// Advance the iterator past the first five elements
	for i := 0; i < 5; i++ {
		iterator.Next()
	}

	// Retrieve a chunk of size 5 and assert the starting msg and the size of the
	// batch returned.
	chunkSize := 5
	_, obBatch := makeBatch(key, iterator, chunkSize)
	encMsgList := obBatch.EncryptedObservation
	if len(encMsgList) != chunkSize {
		t.Errorf("Got chunk of size [%v], expected [%d]", len(encMsgList), chunkSize)
	}

	if !reflect.DeepEqual(encMsgList[0], obVals[5].EncryptedObservation) {
		t.Errorf("Got [%v], expected [%v]", encMsgList[0], obVals[5].EncryptedObservation)
	}

	// The iterator is now at elemet 10. Advance the iterator to element 27.
	for i := 0; i < 17; i++ {
		iterator.Next()
	}
	_, obBatch = makeBatch(key, iterator, chunkSize)
	encMsgList = obBatch.EncryptedObservation
	if len(encMsgList) != 3 {
		t.Errorf("Got chunk size [%v], expected chunk size [3]", len(encMsgList))
		return
	}

	if !reflect.DeepEqual(encMsgList[0], obVals[27].EncryptedObservation) {
		t.Errorf("Got [%v], expected [%v]", encMsgList[0], obVals[27].EncryptedObservation)
	}

	if err := iterator.Release(); err != nil {
		t.Errorf("got error while releasing iterator: %v", err)
		return
	}
}
