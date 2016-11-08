package storage

import (
	"context"
	"fmt"
	"reflect"
	"sync"

	shufflerpb "cobalt"
)

// The MemStore struct is a valid in-memory implementation of the Shuffler Data
// Layer indexed by unique ObservationMetadata key.
type MemStore struct {
	// ObservationsMap is a map indexed by ObersvationMetadata to a list of
	// sealed EncryptedMessages.
	observationsMap map[shufflerpb.ObservationMetadata][]*shufflerpb.EncryptedMessage

	// mu is the global mutex that protects all elements of the store
	mu sync.RWMutex
}

// NewMemStore creates an empty MemStore.
func NewMemStore() *MemStore {
	return &MemStore{
		observationsMap: make(map[shufflerpb.ObservationMetadata][]*shufflerpb.EncryptedMessage),
	}
}

// AddObservation inserts an encrypted message for a given ObservationMetadata.
func (store *MemStore) AddObservation(ctx context.Context, om shufflerpb.ObservationMetadata, em *shufflerpb.EncryptedMessage) error {
	store.mu.Lock()
	defer store.mu.Unlock()

	store.observationsMap[om] = append(store.observationsMap[om], em)
	return nil
}

// GetObservations retrieves the list of EncryptedMessages from the store
// corresponding to a given ObservationMetadata key.
func (store *MemStore) GetObservations(_ context.Context, om shufflerpb.ObservationMetadata) ([]*shufflerpb.EncryptedMessage, error) {
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
// shuffler datastore.
func (store *MemStore) GetKeys(_ context.Context) []shufflerpb.ObservationMetadata {
	store.mu.RLock()
	defer store.mu.RUnlock()

	keys := []shufflerpb.ObservationMetadata{}
	for k := range store.observationsMap {
		keys = append(keys, k)
	}
	return keys
}

// EraseAll deletes both the ObservationMetadata key and all its
// EncryptedMessages.
func (store *MemStore) EraseAll(_ context.Context, om shufflerpb.ObservationMetadata) error {
	store.mu.Lock()
	defer store.mu.Unlock()

	delete(store.observationsMap, om)
	return nil
}
