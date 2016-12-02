package storage

import (
	"fmt"
	"reflect"
	"sync"

	shufflerpb "cobalt"
)

// MemStore is an in-memory implementation of the Store interface.
type MemStore struct {
	// ObservationsMap is a map indexed by ObersvationMetadata to a list of
	// ObservationInfo that contains sealed EncryptedMessages along with creation
	// timestamp.
	observationsMap map[shufflerpb.ObservationMetadata][]*ObservationInfo

	// mu is the global mutex that protects all elements of the store
	mu sync.RWMutex
}

// NewMemStore creates an empty MemStore.
func NewMemStore() *MemStore {
	return &MemStore{
		observationsMap: make(map[shufflerpb.ObservationMetadata][]*ObservationInfo),
	}
}

// AddObservation inserts |observationInfo| into MemStore under the key
// |metadata|.
func (store *MemStore) AddObservation(om shufflerpb.ObservationMetadata, obInfo *ObservationInfo) error {
	store.mu.Lock()
	defer store.mu.Unlock()

	store.observationsMap[om] = append(store.observationsMap[om], obInfo)
	return nil
}

// GetObservations retrieves the list of ObservationInfos from MemStore
// for the given |metadata| key.
func (store *MemStore) GetObservations(om shufflerpb.ObservationMetadata) ([]*ObservationInfo, error) {
	store.mu.RLock()
	defer store.mu.RUnlock()

	for key, val := range store.observationsMap {
		if reflect.DeepEqual(key, om) {
			return val, nil
		}
	}

	return nil, fmt.Errorf("Metric %v doesn't exist in data store.", om)
}

// GetKeys returns the list of unique ObservationMetadata keys stored in
// MemStore.
func (store *MemStore) GetKeys() []shufflerpb.ObservationMetadata {
	store.mu.RLock()
	defer store.mu.RUnlock()

	keys := []shufflerpb.ObservationMetadata{}
	for k := range store.observationsMap {
		keys = append(keys, k)
	}
	return keys
}

// EraseAll deletes both the |metadata| key and all it's ObservationInfos from
// MemStore.
func (store *MemStore) EraseAll(om shufflerpb.ObservationMetadata) error {
	store.mu.Lock()
	defer store.mu.Unlock()

	delete(store.observationsMap, om)
	return nil
}
