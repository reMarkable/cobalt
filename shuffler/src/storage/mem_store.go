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

package storage

import (
	"fmt"
	"math/rand"
	"strconv"
	"sync"

	"github.com/golang/glog"
	"github.com/golang/protobuf/proto"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"

	"cobalt"
	"shuffler"
	rand_util "util"
)

var randGen rand_util.Random

// MemStore is an in-memory implementation of the Store interface.
type MemStore struct {
	// ObservationsMap is a map for storing observations. Map keys are serialized
	// |ObservationMetadata| strings that point to a map of |ObservationVal|s.
	//
	// Map keys for |ObservationVal| map are the same identifiers that uniquely
	// represent the |ObservationVal| in the data store.
	observationsMap map[string]map[string]*shuffler.ObservationVal

	// mu is the global mutex that protects all elements of the store
	mu sync.RWMutex
}

// NewMemStore creates an empty MemStore.
func NewMemStore() *MemStore {
	randGen = rand_util.NewDeterministicRandom(int64(1))

	return &MemStore{
		observationsMap: make(map[string]map[string]*shuffler.ObservationVal),
	}
}

// Key returns the text representation of the given |ObservationMetadata|.
func key(om *cobalt.ObservationMetadata) string {
	if om == nil {
		return ""
	}

	return proto.CompactTextString(om)
}

// shuffle returns a random ordering of input ObservationVals.
func shuffle(obVals []*shuffler.ObservationVal) []*shuffler.ObservationVal {
	numObservations := len(obVals)

	// Get a random ordering for all messages. We assume that the random
	// number generator is appropriately seeded.
	perm := rand.Perm(numObservations)

	shuffledObservations := make([]*shuffler.ObservationVal, numObservations)
	for i, rnd := range perm {
		shuffledObservations[i] = obVals[rnd]
	}

	return shuffledObservations
}

// AddAllObservations adds all of the encrypted observations in all of the
// ObservationBatches in |envelopeBatch| to the store. New |ObservationVal|s
// are created to hold the values and the given |arrivalDayIndex|. Returns a
// non-nil error if the arguments are invalid or the operation fails.
func (store *MemStore) AddAllObservations(envelopeBatch []*cobalt.ObservationBatch, dayIndex uint32) error {
	store.mu.Lock()
	defer store.mu.Unlock()

	for _, batch := range envelopeBatch {
		if batch != nil {
			om := batch.GetMetaData()
			if om == nil {
				return grpc.Errorf(codes.InvalidArgument, "One of the ObservationBatches did not have meta_data set")
			}
			glog.V(3).Infoln(fmt.Sprintf("Received a batch of %d encrypted Observations.", len(batch.GetEncryptedObservation())))
			for _, encryptedObservation := range batch.GetEncryptedObservation() {
				if encryptedObservation == nil {
					return grpc.Errorf(codes.InvalidArgument, "The ObservationBatch with key %v contained a Null encrypted_observation", om)
				}

				id, err := randGen.RandomUint63(1<<63 - 1)
				if err != nil {
					return grpc.Errorf(codes.Internal, "Error in generating unique identifier for key [%v]: %v", om, err)
				}

				valMap, ok := store.observationsMap[key(om)]
				if !ok {
					valMap = make(map[string]*shuffler.ObservationVal)
					store.observationsMap[key(om)] = valMap
				}
				idStr := strconv.Itoa(int(id))
				valMap[idStr] = NewObservationVal(encryptedObservation, idStr, dayIndex)
			}
		}
	}

	return nil
}

// GetObservations returns a MemStoreIterator to iterate through the shuffled
// list of ObservationVals from the data store for the given
// |ObservationMetadata| key or returns an error.
func (store *MemStore) GetObservations(om *cobalt.ObservationMetadata) (Iterator, error) {
	store.mu.RLock()
	defer store.mu.RUnlock()

	if om == nil {
		panic("om is nil")
	}

	// get ObservationVal map for the given key
	valMap, present := store.observationsMap[key(om)]
	if !present {
		return nil, grpc.Errorf(codes.InvalidArgument, "Key %v not found", om)
	}

	// make return slice from ObservationVal map
	var obVals []*shuffler.ObservationVal
	for _, val := range valMap {
		obVals = append(obVals, val)
	}

	// Shuffler data store layer guarantees that the list returned on Get() call
	// is always shuffled. In memstore, this is acheieved by shuffling the
	// |ObservationVal| result set.
	iter := NewMemStoreIterator(shuffle(obVals))

	return iter, nil
}

// GetKeys returns the list of all |ObservationMetadata| keys stored in the
// data store or returns an error.
func (store *MemStore) GetKeys() ([]*cobalt.ObservationMetadata, error) {
	store.mu.RLock()
	defer store.mu.RUnlock()

	keys := []*cobalt.ObservationMetadata{}
	for k := range store.observationsMap {
		om := &cobalt.ObservationMetadata{}
		err := proto.UnmarshalText(k, om)
		if err != nil {
			return nil, grpc.Errorf(codes.Internal, "Error in parsing keys: %v", err)
		}
		keys = append(keys, om)
	}
	return keys, nil
}

// DeleteValues deletes the given |ObservationVal|s for |ObservationMetadata|
// key from the data store or returns an error.
func (store *MemStore) DeleteValues(om *cobalt.ObservationMetadata, deleteObVals []*shuffler.ObservationVal) error {
	store.mu.Lock()
	defer store.mu.Unlock()

	if om == nil {
		panic("om is nil")
	}

	valMap, present := store.observationsMap[key(om)]
	if !present {
		return grpc.Errorf(codes.InvalidArgument, "Key %v not found", om)
	}

	for _, obVal := range deleteObVals {
		delete(valMap, obVal.Id)
	}

	if len(valMap) == 0 {
		delete(store.observationsMap, key(om))
	}

	return nil
}

// GetNumObservations returns the total count of ObservationVals in the data
// store for the given |ObservationMmetadata| key or returns an error.
func (store *MemStore) GetNumObservations(om *cobalt.ObservationMetadata) (int, error) {
	store.mu.RLock()
	defer store.mu.RUnlock()

	if om == nil {
		panic("om is nil")
	}

	valMap, present := store.observationsMap[key(om)]
	if !present {
		return 0, grpc.Errorf(codes.InvalidArgument, "Key %v not found", om)
	}

	return len(valMap), nil
}

// Reset clears the existing in-memory state for |store|.
func (store *MemStore) Reset() {
	store.mu.Lock()
	defer store.mu.Unlock()

	store.observationsMap = make(map[string]map[string]*shuffler.ObservationVal)
}
