package storage

import (
	"context"
	"math/rand"
	"strconv"
	"strings"
	"sync"
	"testing"

	shufflerpb "cobalt"
)

// createObservationMetaData constructs fake observation metadata for testing.
func createObservationMetaData(customerID int, projectID int, metricID int, dayIndex int) shufflerpb.ObservationMetadata {
	return shufflerpb.ObservationMetadata{
		CustomerId: uint32(customerID),
		ProjectId:  uint32(projectID),
		MetricId:   uint32(metricID),
		DayIndex:   uint32(dayIndex),
	}
}

// createRandomEncryptedMsg constructs fake encrypted message for testing.
func createRandomEncryptedMsg(pubKey string) *shufflerpb.EncryptedMessage {
	var bytes = make([]byte, 20)
	rand.Read(bytes)
	return &shufflerpb.EncryptedMessage{
		Scheme:     shufflerpb.EncryptedMessage_PK_SCHEME_1,
		PubKey:     pubKey,
		Ciphertext: bytes}
}

// CreateAndInsertObservation creates and inserts a fake observation into
// in-memory datastore.
func createAndInsertObservation(store *MemStore, ctx context.Context, i int,
	t *testing.T) {
	if err := store.AddObservation(ctx,
		createObservationMetaData(i, 500+i, 1000+i, i%7),
		createRandomEncryptedMsg(strings.Join([]string{"pk"}, strconv.Itoa(i)))); err != nil {
		t.Errorf("got error %v, expected success", err)
	}
}

// TestEmptyStore tests that the store returns an error when we try to get a
// status from an empty store.
func TestEmptyStore(t *testing.T) {
	store := NewMemStore()

	if metrics := store.GetKeys(context.Background()); len(metrics) != 0 {
		t.Errorf("got response with size %d, expected empty list", len(metrics))
	}
}

// TestAddAndDeleteObservations tests that the store correctly handles insert,
// get and delete requests.
func TestAddGetAndDeleteForSingleObservation(t *testing.T) {
	store := NewMemStore()
	ctx := context.Background()

	// Add same observations
	om := createObservationMetaData(1, 501, 1001, 2)
	em := createRandomEncryptedMsg("pkey1")
	if err := store.AddObservation(ctx, om, em); err != nil {
		t.Errorf("got error %v, expected success", err)
	}

	// Retrieve stored observation contents
	if _, err := store.GetObservations(ctx, om); err != nil {
		t.Errorf("got error %v, expected list of observations", err)
	}

	// Get list of saved metrics
	if metrics := store.GetKeys(ctx); len(metrics) == 0 {
		t.Errorf("got empty list, expected metric: %v", om)
	}

	// Remove metric
	if err := store.EraseAll(ctx, om); err != nil {
		t.Errorf("got error %v, expected successful deletion", err)
	}

	// Get list of saved metrics, now it should return empty list
	if metrics := store.GetKeys(ctx); len(metrics) != 0 {
		t.Errorf("got response with size %d, expected empty list", len(metrics))
	}
}

// TestAddAndDeleteObservations tests that the store correctly handles insert,
// get and delete requests.
func TestAddGetAndDeleteForMultipleObservations(t *testing.T) {
	store := NewMemStore()
	ctx := context.Background()
	size := 10

	// Generating fake metrics
	for i := 1; i <= size; i++ {
		createAndInsertObservation(store, ctx, i, t)
	}

	// Adding multiple encrypted messages for half of the existing metrics
	randIndex := 0
	for i := 1; i <= size/2; i++ {
		// Pick a random metadata index
		seed := rand.NewSource(42)
		rnd := rand.New(seed)
		for {
			randIndex = rnd.Intn(size)
			if randIndex != 0 {
				break
			}
		}
		// Add 4 more messages to the existing matadata content
		for j := 0; j < 4; j++ {
			createAndInsertObservation(store, ctx, randIndex, t)
		}
	}

	// Get list of saved metrics
	metrics := []shufflerpb.ObservationMetadata{}
	if metrics = store.GetKeys(ctx); len(metrics) == 0 {
		t.Errorf("got empty list, expecting valid metrics")
	} else if len(metrics) != size {
		t.Errorf("got [%d] metrics, expected [%d]", len(metrics), size)
	}

	// Retrieve stored observation contents for a sample msg
	om := createObservationMetaData(1, 501, 1001, 1%7)
	if _, err := store.GetObservations(ctx, om); err != nil {
		t.Errorf("got error [%v], expected valid list of observations", err)
	}

	// Retrieve stored observation contents for a key with different msgs
	om = createObservationMetaData(randIndex, 500+randIndex, 1000+randIndex, randIndex%7)
	emListLen := 0
	if emList, err := store.GetObservations(ctx, om); err != nil {
		t.Errorf("got error [%v], expected a non-zero list of observations", err)
	} else if len(emList) <= 1 {
		t.Errorf("got [%d] observations, expected more than one observation per metric", len(emList))
	} else {
		emListLen = len(emList)
		t.Logf("got [%d] observations", emListLen)
	}

	// Remove a metric and its associated list of observations
	if err := store.EraseAll(ctx, om); err != nil {
		t.Errorf("got error %v, expected successful deletion for metric [%v]", err, om)
	}

	// Retrieve stored observation contents for the deleted metric
	if emList, err := store.GetObservations(ctx, om); len(emList) != 0 && len(emList) == emListLen {
		t.Errorf("got [%d] observations, expected [0]", len(emList))
	} else if err != nil && !strings.Contains(err.Error(), "doesn't exist in data store.") {
		t.Errorf("got error [%v], expected error [Metric %v doesn't exist in data store.]", err, om)
	}

	// Get list of saved metrics after deletion
	if metrics = store.GetKeys(ctx); len(metrics) == 0 {
		t.Errorf("got empty list, expecting valid metrics")
	} else if len(metrics) != size-1 {
		t.Errorf("got [%d] metrics, expected [%d]", len(metrics), size-1)
	}
}

// TestMemStoreConcurrency tests that the store correctly handles multiple
// processes accessing the same DB instance
func TestMemStoreConcurrency(t *testing.T) {
	ctx := context.Background()
	store := NewMemStore()

	// Launch 100 goroutines to simulate multiple instances trying to insert
	// concurrently.
	var wg sync.WaitGroup
	for i := 1; i <= 100; i++ {
		wg.Add(1)
		go func(store *MemStore, ctx context.Context, index int, t *testing.T) {
			defer wg.Done()
			createAndInsertObservation(store, ctx, index, t)
		}(store, ctx, i, t)
	}
	wg.Wait()

	// Check that all 100 metrics are saved correctly.
	lookupAndVerify := func(store *MemStore, ctx context.Context, index int, t *testing.T) {
		om := createObservationMetaData(index, 500+index, 1000+index, index%7)
		if emList, err := store.GetObservations(ctx, om); err != nil {
			t.Errorf("got error %v, expected non-empty content", err)
		} else if len(emList) != 1 {
			t.Errorf("got [%d] encrypted messages, expected [1] encrypted message", len(emList))
		}
	}

	for i := 1; i <= 100; i++ {
		wg.Add(1)
		go func(store *MemStore, ctx context.Context, index int, t *testing.T) {
			defer wg.Done()
			lookupAndVerify(store, ctx, index, t)
		}(store, ctx, i, t)
	}
	wg.Wait()

	// Delete 5 metrics concurrently
	deleteAndVerify := func(store *MemStore, ctx context.Context, index int, t *testing.T) {
		om := createObservationMetaData(index, 500+index, 1000+index, index%7)
		if err := store.EraseAll(ctx, om); err != nil {
			t.Errorf("got error %v, expected successful deletion for metric [%v]", err, om)
		}
	}

	for i := 1; i <= 5; i++ {
		wg.Add(1)
		go func(store *MemStore, ctx context.Context, index int, t *testing.T) {
			defer wg.Done()
			deleteAndVerify(store, ctx, index, t)
		}(store, ctx, i*7, t)
	}
	wg.Wait()

	// Retrieve stored observation contents for oneof the deleted metrics
	deletedIndex := 14
	om := createObservationMetaData(deletedIndex, 500+deletedIndex, 1000+deletedIndex, deletedIndex%7)
	if _, err := store.GetObservations(ctx, om); err != nil && !strings.Contains(err.Error(), "doesn't exist in data store.") {
		t.Errorf("got error [%v], expected error [Metric %v doesn't exist in data store.]", err, om)
	}

	// Verify count of saved metrics after concurrent deletion
	if metrics := store.GetKeys(ctx); len(metrics) != 95 {
		t.Errorf("got [%d] metrics, expected [95]", len(metrics))
	}
}
