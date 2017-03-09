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
	"crypto/rand"
	"fmt"
	"reflect"
	"strconv"
	"strings"
	"testing"
	"time"

	shufflerpb "cobalt"
	"storage"
)

func TestReadyToDispatch(t *testing.T) {
	now := time.Now()

	// Dispatch frequency set to 0, always dispatch!
	if !readyToDispatch(0, &now) {
		t.Errorf("No interval specified, dispatch should always happen!")
	}

	// Dispatch frequency set to 1 hour, dispatch attempt should fail and last
	// timestamp must not be updated.
	if readyToDispatch(1, &now) {
		t.Errorf("Dispatch attempt must fail, as there is one dispatch attempt in the last 1 hour.")
	}

	// Set the dispatch frequency to 1 hour, and validate that readyToDispatch
	// returns true as the elapsed time is greater than 1 hour.
	oneDayAgo := time.Unix(now.Unix()-(60*60*24*1), 0)
	if !readyToDispatch(1, &oneDayAgo) {
		t.Errorf("Dispatch attempt must succeed, as the last dispatch attempt happened more than an hour ago.")
	}

	// Assert that last timestamp has been modified to the current time.
	if uint32(now.Sub(oneDayAgo).Hours()) != 0 {
		t.Errorf("Last dispatch timestamp must be updated to current time.")
	}

	// Dispatch frequency set to 0, always dispatch!
	if !readyToDispatch(0, &now) {
		t.Errorf("No interval specified, multiple dispatch events can happen within the same hour!")
	}
}

// createRandomObservationInfo constructs fake |ObservationInfo| for testing.
func createRandomObservationInfo(pubKey string, ts time.Time) *storage.ObservationInfo {
	var bytes = make([]byte, 20)
	rand.Read(bytes)
	return &storage.ObservationInfo{
		CreationTimestamp: ts,
		EncryptedMessage: &shufflerpb.EncryptedMessage{
			Scheme:     shufflerpb.EncryptedMessage_NONE,
			PubKey:     pubKey,
			Ciphertext: bytes},
	}
}

func createTestStore(numObservations int) (storage.Store, *shufflerpb.ObservationMetadata, []*storage.ObservationInfo) {
	store := storage.NewMemStore()

	// Add same observations
	key := &shufflerpb.ObservationMetadata{
		CustomerId: uint32(2),
		ProjectId:  uint32(22),
		MetricId:   uint32(222),
		DayIndex:   uint32(3),
	}

	now := time.Now()

	var obInfos []*storage.ObservationInfo
	num := int(numObservations / 3)

	//create 10 with current time
	for i := 0; i < num; i++ {
		obInfos = append(obInfos, createRandomObservationInfo(strings.Join([]string{"pubkey", strconv.Itoa(i)}, "_"), now))
	}

	//create 10 with 3 days ago as the creation timestamp
	for i := 0; i < num; i++ {
		obInfos = append(obInfos, createRandomObservationInfo(strings.Join([]string{"pubkey", strconv.Itoa(i)}, "_"), time.Unix(now.Unix()-(60*60*24*3), 0)))
	}

	//create 10 with 1 day ago as the creation timestamp
	for i := 0; i < num; i++ {
		obInfos = append(obInfos, createRandomObservationInfo(strings.Join([]string{"pubkey", strconv.Itoa(i)}, "_"), time.Unix(now.Unix()-(60*60*24*1), 0)))
	}

	for _, obInfo := range obInfos {
		if err := store.AddObservation(key, obInfo); err != nil {
			fmt.Printf("got error %v, expected AddObservation to be a success", err)
		}
	}

	return store, key, obInfos
}

func TestGetObservationBatchChunk(t *testing.T) {
	key := &shufflerpb.ObservationMetadata{
		CustomerId: uint32(1),
		ProjectId:  uint32(11),
		MetricId:   uint32(111),
		DayIndex:   uint32(2),
	}

	var obInfos []*storage.ObservationInfo
	for i := 0; i < 30; i++ {
		obInfos = append(obInfos, createRandomObservationInfo(strings.Join([]string{"pubkey", strconv.Itoa(i)}, "_"), time.Now()))
	}

	// Retrieve a chunk of size 5 and assert the starting msg and the size of the
	// batch returned.
	chunkSize := 5
	obBatch := getObservationBatchChunk(key, obInfos, 5, 10)
	encMsgList := obBatch.EncryptedObservation
	if len(encMsgList) != chunkSize {
		t.Errorf("Got chunk of size [%v], expected [%d]", len(encMsgList), chunkSize)
	}

	if !reflect.DeepEqual(encMsgList[0], obInfos[5].EncryptedMessage) {
		t.Errorf("Got [%v], expected [%v]", encMsgList[0], obInfos[5].EncryptedMessage)
	}
}

func TestUpdateObservations(t *testing.T) {
	store, key, obInfos := createTestStore(30)

	var obInfosLen int
	var err error
	if obInfosLen, err = store.GetNumObservations(key); err != nil {
		t.Errorf("got error [%v], expected multiple observations", err)
	} else {
		if obInfosLen <= 1 {
			t.Errorf("got [%d] ObservationInfos, expected more than one ObservationInfo per metric", obInfosLen)
		}
	}

	err = updateObservations(store, key, obInfos, uint32(2))
	if err != nil {
		t.Errorf("Expected successful update, got error [%v]", err)
	}

	if obInfosLen, err = store.GetNumObservations(key); err != nil {
		t.Errorf("got error [%v], expected 20 observations", err)
	} else {
		if obInfosLen != 20 {
			t.Errorf("got [%d] ObservationInfos, expected 20 filtered ObservationInfos after disposing 10 observations", obInfosLen)
		}
	}
}

// This is a fake Analyzer object that just caches the Observations in the order
// they are received. This lets us verify the output of the dispatcher.
type MockAnalyzer struct {
	obBatch []*shufflerpb.ObservationBatch
	numSent int
}

func (a *MockAnalyzer) send(obBatch *shufflerpb.ObservationBatch) error {
	a.numSent++
	a.obBatch = append(a.obBatch, obBatch)
	return nil
}

func TestDispatch(t *testing.T) {
	// testconfig for immediate dispatch for smaller batches
	testConfig := &shufflerpb.ShufflerConfig{}
	testConfig.GlobalConfig = &shufflerpb.Policy{
		FrequencyInHours: 0,
		PObservationDrop: 0.0,
		Threshold:        0,
		AnalyzerUrl:      "localhost",
		DisposalAgeDays:  100,
	}

	// Test dispatch for different batch sizes
	num := 30
	analyzer := MockAnalyzer{numSent: 0}

	for _, batchSize := range []int{1, 15, 30, 100} {
		// Generate test store with |num| entries
		store, key, obInfos := createTestStore(num)
		if len(obInfos) != num {
			t.Errorf("BatchSize: [%d], got observations [%v], expected [%v]", batchSize, len(obInfos), num)
		}

		dispatchInternal(testConfig, store, batchSize, &analyzer)

		// check if all msgs are sent
		if num >= batchSize {
			if len(analyzer.obBatch) != num/batchSize {
				t.Errorf("BatchSize: [%d], unexpected number of observations dispatched, got [%v] expected [%v]", batchSize, len(analyzer.obBatch), num)
			}
		} else if len(analyzer.obBatch) != 1 {
			t.Errorf("BatchSize: [%d], expected all observations to be sent at once, got [%v] expected [1]", batchSize, len(analyzer.obBatch))
		}
		// check if the data is sent in batches based on batchSize
		if num >= batchSize {
			if analyzer.numSent != num/batchSize {
				t.Errorf("BatchSize: [%d], unexpected number of analyzer send calls, got [%d], want [%d]", batchSize, analyzer.numSent, num/batchSize)
			}
		} else if analyzer.numSent != 1 {
			t.Errorf("BatchSize: [%d], expected all observations to be sent at once, got [%d], want [1]", batchSize, analyzer.numSent)
		}
		// make sure that all sent msgs are deleted from the Shuffler datastore
		if obInfosLen, err := store.GetNumObservations(key); err == nil || obInfosLen > 0 {
			t.Errorf("BatchSize: [%d], got error [%v] and bucketSize [%d], expected empty observations in the store", batchSize, err, obInfosLen)
		}
		analyzer.numSent = 0
		analyzer.obBatch = nil
	}

	// Test dispatch for different thresholds
	batchSize := 30
	for _, threshold := range []int{1, 10, 30, 100} {
		testConfig.GlobalConfig.Threshold = uint32(threshold)
		// Generate test store with |num| entries
		store, key, obInfos := createTestStore(num)
		if len(obInfos) != num {
			t.Errorf("Threshold: [%d], got observations [%v], expected [%v]", threshold, len(obInfos), num)
		}

		dispatchInternal(testConfig, store, batchSize, &analyzer)

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
			if obInfosLen, err := store.GetNumObservations(key); err == nil || obInfosLen > 0 {
				t.Errorf("Threshold: [%d], got error [%v] and bucketSize [%d], expected empty observations in the store", threshold, err, obInfosLen)
			}
		}
		analyzer.numSent = 0
		analyzer.obBatch = nil
	}
}
