package storage

import (
	"math/rand"
	"reflect"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	shufflerpb "cobalt"
)

// createObservationMetaData constructs fake observation metadata for testing.
func createObservationMetaData(customerID int, projectID int, metricID int, dayIndex int) *shufflerpb.ObservationMetadata {
	return &shufflerpb.ObservationMetadata{
		CustomerId: uint32(customerID),
		ProjectId:  uint32(projectID),
		MetricId:   uint32(metricID),
		DayIndex:   uint32(dayIndex),
	}
}

// createRandomObservationInfo constructs fake |ObservationInfo| for testing.
func createRandomObservationInfo(pubKey string) *ObservationInfo {
	var bytes = make([]byte, 20)
	rand.Read(bytes)
	return &ObservationInfo{
		CreationTimestamp: time.Now(),
		EncryptedMessage: &shufflerpb.EncryptedMessage{
			Scheme:     shufflerpb.EncryptedMessage_NONE,
			PubKey:     pubKey,
			Ciphertext: bytes},
	}
}

// CreateAndInsertObservation creates and inserts a fake observation into
// in-memory datastore.
func createAndInsertObservation(store *MemStore, i int,
	t *testing.T) {
	if err := store.AddObservation(
		createObservationMetaData(i, 500+i, 1000+i, i%7),
		createRandomObservationInfo(strings.Join([]string{"pk"}, strconv.Itoa(i)))); err != nil {
		t.Errorf("got error %v, expected success", err)
	}
}

// TestEmptyStore tests that the store returns an error when we try to get a
// status from an empty store.
func TestEmptyStore(t *testing.T) {
	store := NewMemStore()

	if metrics := store.GetKeys(); len(metrics) != 0 {
		t.Errorf("got response with size %d, expected empty list", len(metrics))
	}
}

// TestAddAndDeleteObservations tests that the store correctly handles insert,
// get and delete requests.
func TestAddGetAndDeleteForSingleObservation(t *testing.T) {
	store := NewMemStore()

	// Add same observations
	om := createObservationMetaData(1, 501, 1001, 2)
	obInfo := createRandomObservationInfo("pkey1")
	if err := store.AddObservation(om, obInfo); err != nil {
		t.Errorf("got error %v, expected success", err)
	}

	// Validate the size of observations
	if obInfoSize, err := store.GetNumObservations(om); err != nil {
		t.Errorf("got error %v, expected atleast one observation", err)
	} else {
		if obInfoSize != 1 {
			t.Errorf("got observation count [%d], expected only one observation", obInfoSize)
		}
	}

	// Retrieve and validate stored observation contents
	if gotObInfo, err := store.GetObservations(om); err != nil {
		t.Errorf("got error %v, expected atleast one observation", err)
	} else {
		if len(gotObInfo) != 1 {
			t.Errorf("got multiple observations with size [%d], expected only one observation", len(gotObInfo))
		}

		if !reflect.DeepEqual(gotObInfo[0], obInfo) {
			t.Errorf("Got response [%v], expected [%v]", gotObInfo, obInfo)
		}
	}

	// Get list of saved metrics
	if metrics := store.GetKeys(); len(metrics) == 0 {
		t.Errorf("got empty list, expected metric: %v", om)
	}

	// Remove metric
	if err := store.EraseAll(om); err != nil {
		t.Errorf("got error %v, expected successful deletion", err)
	}

	// Get list of saved metrics, now it should return empty list
	if metrics := store.GetKeys(); len(metrics) != 0 {
		t.Errorf("got response with size %d, expected empty list", len(metrics))
	}
}

// TestAddAndDeleteObservations tests that the store correctly handles insert,
// get and delete requests.
func TestAddGetAndDeleteForMultipleObservations(t *testing.T) {
	store := NewMemStore()
	size := 10

	// Generating fake metrics
	for i := 1; i <= size; i++ {
		createAndInsertObservation(store, i, t)
	}

	// Adding multiple ObservationInfos for half of the existing metrics
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
			createAndInsertObservation(store, randIndex, t)
		}
	}

	// Get list of saved metrics
	if metrics := store.GetKeys(); len(metrics) == 0 {
		t.Errorf("got empty list, expecting valid metrics")
	} else if len(metrics) != size {
		t.Errorf("got [%d] metrics, expected [%d]", len(metrics), size)
	}

	// Retrieve stored observation contents for a sample msg
	om := createObservationMetaData(1, 501, 1001, 1%7)
	if _, err := store.GetObservations(om); err != nil {
		t.Errorf("got error [%v], expected valid list of observations", err)
	}

	// Retrieve stored observation contents for a key with different msgs
	om = createObservationMetaData(randIndex, 500+randIndex, 1000+randIndex, randIndex%7)

	var obInfosLen int
	var err error
	if obInfosLen, err = store.GetNumObservations(om); err != nil {
		t.Errorf("got error [%v], expected multiple observations", err)
	} else {
		if obInfosLen <= 1 {
			t.Errorf("got [%d] ObservationInfos, expected more than one ObservationInfo per metric", obInfosLen)
		}
	}

	if obInfos, err := store.GetObservations(om); err != nil {
		t.Errorf("got error [%v], expected a non-zero list of observations", err)
	} else if obInfosLen != len(obInfos) {
		t.Errorf("got [%d] observations, expected [%d] observations", len(obInfos), obInfosLen)
	}

	// Remove a metric and its associated list of observations
	if err := store.EraseAll(om); err != nil {
		t.Errorf("got error %v, expected successful deletion for metric [%v]", err, om)
	}

	// Retrieve stored observation contents for the deleted metric
	if obInfos, err := store.GetObservations(om); len(obInfos) != 0 && len(obInfos) == obInfosLen {
		t.Errorf("got [%d] observations, expected [0]", len(obInfos))
	} else if err != nil && !strings.Contains(err.Error(), "doesn't exist in data store.") {
		t.Errorf("got error [%v], expected error [Metric %v doesn't exist in data store.]", err, om)
	}

	// Get list of saved metrics after deletion
	if metrics := store.GetKeys(); len(metrics) == 0 {
		t.Errorf("got empty list, expecting valid metrics")
	} else if len(metrics) != size-1 {
		t.Errorf("got [%d] metrics, expected [%d]", len(metrics), size-1)
	}
}

// TestMemStoreConcurrency tests that the store correctly handles multiple
// processes accessing the same DB instance
func TestMemStoreConcurrency(t *testing.T) {
	store := NewMemStore()

	// Launch 100 goroutines to simulate multiple instances trying to insert
	// concurrently.
	var wg sync.WaitGroup
	for i := 1; i <= 100; i++ {
		wg.Add(1)
		go func(store *MemStore, index int, t *testing.T) {
			defer wg.Done()
			createAndInsertObservation(store, index, t)
		}(store, i, t)
	}
	wg.Wait()

	// Check that all 100 metrics are saved correctly.
	lookupAndVerify := func(store *MemStore, index int, t *testing.T) {
		om := createObservationMetaData(index, 500+index, 1000+index, index%7)
		if obInfos, err := store.GetObservations(om); err != nil {
			t.Errorf("got error %v, expected non-empty content", err)
		} else if len(obInfos) != 1 {
			t.Errorf("got [%d] ObservationInfos, expected [1] ObservationInfo", len(obInfos))
		}
	}

	for i := 1; i <= 100; i++ {
		wg.Add(1)
		go func(store *MemStore, index int, t *testing.T) {
			defer wg.Done()
			lookupAndVerify(store, index, t)
		}(store, i, t)
	}
	wg.Wait()

	// Delete 5 metrics concurrently
	deleteAndVerify := func(store *MemStore, index int, t *testing.T) {
		om := createObservationMetaData(index, 500+index, 1000+index, index%7)
		if err := store.EraseAll(om); err != nil {
			t.Errorf("got error %v, expected successful deletion for metric [%v]", err, om)
		}
	}

	for i := 1; i <= 5; i++ {
		wg.Add(1)
		go func(store *MemStore, index int, t *testing.T) {
			defer wg.Done()
			deleteAndVerify(store, index, t)
		}(store, i*7, t)
	}
	wg.Wait()

	// Retrieve stored observation contents for oneof the deleted metrics
	deletedIndex := 14
	om := createObservationMetaData(deletedIndex, 500+deletedIndex, 1000+deletedIndex, deletedIndex%7)
	if _, err := store.GetObservations(om); err != nil && !strings.Contains(err.Error(), "doesn't exist in data store.") {
		t.Errorf("got error [%v], expected error [Metric %v doesn't exist in data store.]", err, om)
	}

	// Verify count of saved metrics after concurrent deletion
	if metrics := store.GetKeys(); len(metrics) != 95 {
		t.Errorf("got [%d] metrics, expected [95]", len(metrics))
	}
}

// TestShuffle validates the shuffling functionality.
func TestShuffle(t *testing.T) {
	// Make test deterministic
	rand.Seed(1)

	num := 10
	// Create the input test ObservationInfos.
	testObInfos := make([][]*ObservationInfo, 2)
	// empty list
	testObInfos[0] = append(testObInfos[0], &ObservationInfo{})
	// list with 10 items
	for i := 0; i < num; i++ {
		testObInfos[1] = append(testObInfos[1], createRandomObservationInfo(strings.Join([]string{"pubkey", strconv.Itoa(i)}, "-")))
	}

	for _, testObInfo := range testObInfos {
		shuffledObInfo := shuffle(testObInfo)

		// Check that basic shuffling occurred
		// TODO(bittau) define the acceptance criteria for how good a shuffle is
		// and assert them.
		if reflect.DeepEqual(shuffledObInfo, testObInfo) {
			// Skip empty lists
			if len(testObInfo) >= 1 && testObInfo[0].EncryptedMessage != nil {
				t.Error("Error in shuffling Observations")
			}
		}
	}
}
