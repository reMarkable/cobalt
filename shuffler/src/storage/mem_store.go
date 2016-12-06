package storage

import (
	"fmt"
	"math/rand"
	"sync"

	shufflerpb "cobalt"

	"github.com/golang/protobuf/proto"
)

// MemStore is an in-memory implementation of the Store interface.
type MemStore struct {
	// ObservationsMap is a map indexed by ObersvationMetadata to a list of
	// ObservationInfo that contains sealed EncryptedMessages along with creation
	// timestamp.
	observationsMap map[string][]*ObservationInfo

	// mu is the global mutex that protects all elements of the store
	mu sync.RWMutex
}

// NewMemStore creates an empty MemStore.
func NewMemStore() *MemStore {
	return &MemStore{
		observationsMap: make(map[string][]*ObservationInfo),
	}
}

// Key returns the text representation of the given |ObservationMetadata|.
func key(om *shufflerpb.ObservationMetadata) string {
	if om == nil {
		return ""
	}

	// TODO(ukode) Change to a more efficient implementation that gives
	// shorter keys.
	return proto.CompactTextString(om)
}

// shuffle shuffles the list of ObservationInfos and returns a random ordering
// of Observations that are sent to the Analyzer.
func shuffle(obInfos []*ObservationInfo) []*ObservationInfo {
	numObservations := len(obInfos)

	// Get a random ordering for all messages. We assume that the random
	// number generator is appropriately seeded.
	perm := rand.Perm(numObservations)

	shuffledObservations := make([]*ObservationInfo, numObservations)
	for i, rnd := range perm {
		shuffledObservations[i] = obInfos[rnd]
	}

	return shuffledObservations
}

// AddObservation inserts |observationInfo| into MemStore under the key
// |metadata|.
func (store *MemStore) AddObservation(om *shufflerpb.ObservationMetadata, obInfo *ObservationInfo) error {
	store.mu.Lock()
	defer store.mu.Unlock()
	if om == nil {
		return nil
	}

	store.observationsMap[key(om)] = append(store.observationsMap[key(om)], obInfo)
	return nil
}

// GetObservations retrieves the list of ObservationInfos from MemStore
// for the given |metadata| key.
func (store *MemStore) GetObservations(om *shufflerpb.ObservationMetadata) ([]*ObservationInfo, error) {
	store.mu.RLock()
	defer store.mu.RUnlock()

	if om == nil {
		return nil, fmt.Errorf("Invalid metric [%v]", *om)
	}

	obInfos, present := store.observationsMap[key(om)]
	if !present {
		return nil, fmt.Errorf("Metric %v doesn't exist in data store.", *om)
	}
	// TODO(ukode) to make this more efficient. There is no reason to shuffle
	// more than once. We should keep track of whether or not a shuffle is
	// required.
	return shuffle(obInfos), nil
}

// GetKeys returns the list of unique ObservationMetadata keys stored in
// MemStore.
func (store *MemStore) GetKeys() []*shufflerpb.ObservationMetadata {
	store.mu.RLock()
	defer store.mu.RUnlock()

	keys := []*shufflerpb.ObservationMetadata{}
	om := &shufflerpb.ObservationMetadata{}
	for k := range store.observationsMap {
		err := proto.UnmarshalText(k, om)
		if err != nil {
			break
		}
		keys = append(keys, om)
	}
	return keys
}

// EraseAll deletes both the |metadata| key and all it's ObservationInfos from
// MemStore.
func (store *MemStore) EraseAll(om *shufflerpb.ObservationMetadata) error {
	store.mu.Lock()
	defer store.mu.Unlock()

	delete(store.observationsMap, key(om))
	return nil
}

// GetNumObservations returns the count of ObservationInfos for a given
// |metadata| key.
func (store *MemStore) GetNumObservations(om *shufflerpb.ObservationMetadata) (int, error) {
	store.mu.RLock()
	defer store.mu.RUnlock()

	if om == nil {
		return 0, fmt.Errorf("Invalid metric: [%v]", *om)
	}

	obInfos, present := store.observationsMap[key(om)]
	if !present {
		return 0, fmt.Errorf("Metric %v doesn't exist in data store.", *om)
	}

	return len(obInfos), nil
}
