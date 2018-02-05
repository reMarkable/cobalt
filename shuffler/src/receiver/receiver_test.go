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

package receiver

import (
	"context"
	"testing"

	"github.com/golang/protobuf/proto"

	shufflerpb "cobalt"
	"storage"
	"util"
)

// envelopeData represents a test Envelope to be passed to the Receiver.process()
// method and the list of expected bucket keys for the buckets that should be
// created.
type envelopeData struct {
	envelope           *shufflerpb.Envelope
	expectedBucketKeys []shufflerpb.ObservationMetadata
}

// makeEnvelope creates an Envelope containing |numBatches| ObservationBatches
// containing |numObservationsPerBatch| EncryptedMessages each containing
// random ciphertext bytes. It returns an envelopeData containing the newly
// constructed Envelope and the expected list of keys for the store buckets that
// should be created when the Receiver processes the Envelope.
func makeEnvelope(numBatches int, numObservationsPerBatch int) envelopeData {
	var batch []*shufflerpb.ObservationBatch
	var expectedBucketKeys []shufflerpb.ObservationMetadata
	for i := 1; i <= numBatches; i++ {
		metadata := storage.NewObservationMetaData(i)
		expectedBucketKeys = append(expectedBucketKeys, *metadata)
		// We set the SystemProfile in the ObservationMetadata to nil to simulate
		// the fact that this is what the Encoder sends to the Shuffler in an
		// Envelope. We want to test that the Receiver will correctly copy the SystemProfile
		// from the Envelope into each ObservationMetadata.
		metadata.SystemProfile = nil
		batch = append(batch, &shufflerpb.ObservationBatch{
			MetaData:             metadata,
			EncryptedObservation: storage.MakeRandomEncryptedMsgs(numObservationsPerBatch),
		})
	}

	return envelopeData{
		envelope:           &shufflerpb.Envelope{SystemProfile: storage.NewFakeSystemProfile(), Batch: batch},
		expectedBucketKeys: expectedBucketKeys,
	}
}

// makeTestEnvelopes creates sample envelopes with different configuration for
// testing.
func makeTestEnvelopes() []envelopeData {
	emptyEnvelope := makeEnvelope(0, 0)
	envelopeWithOneObservation := makeEnvelope(1, 1)
	envelopeWithMultipleObservations := makeEnvelope(1, 7)
	envelopeWithHybridObservations := makeEnvelope(10, 5)
	return []envelopeData{emptyEnvelope, envelopeWithOneObservation, envelopeWithMultipleObservations, envelopeWithHybridObservations}
}

func TestMemStoreShuffler(t *testing.T) {
	for _, envelopeData := range makeTestEnvelopes() {
		doTestProcess(t, envelopeData.envelope, envelopeData.expectedBucketKeys, storage.NewMemStore())
	}
}

func TestLevelDBShuffler(t *testing.T) {
	for _, envelopeData := range makeTestEnvelopes() {
		levelDBStore, err := storage.NewLevelDBStore("/tmp/receiver_db")
		if err != nil {
			t.Errorf("Failed to initialize leveldb store")
			return
		}
		doTestProcess(t, envelopeData.envelope, envelopeData.expectedBucketKeys, levelDBStore)
	}
}

func doTestProcess(t *testing.T, envelope *shufflerpb.Envelope,
	expectedBucketKeys []shufflerpb.ObservationMetadata, store storage.Store) {
	data, err := proto.Marshal(envelope)
	if err != nil {
		t.Fatalf("Error in marshalling envelope data: %v", err)
	}
	eMsg := &shufflerpb.EncryptedMessage{
		Ciphertext: data, // test unencrypted envelope
		Scheme:     shufflerpb.EncryptedMessage_NONE,
	}

	shuffler := &ShufflerServer{
		store: store,
		config: ServerConfig{
			EnableTLS: false,
			CertFile:  "",
			KeyFile:   "",
			Port:      0,
		},
		decrypter: util.NewMessageDecrypter(""),
	}

	expectErr := len(envelope.GetBatch()) == 0
	_, err = shuffler.Process(context.Background(), eMsg)

	if expectErr && err == nil {
		t.Fatalf("Expected Process() to return an error for envelope |%v|", envelope)
	}

	if !expectErr && err != nil {
		t.Fatalf("Unexpected error returned from Process() for envelope |%v|: %v", envelope, err)
	}

	for i, batch := range envelope.GetBatch() {
		numObservations := len(batch.GetEncryptedObservation())
		if numObservations == 0 {
			continue
		}

		key := expectedBucketKeys[i]
		storage.CheckNumObservations(t, shuffler.store, &key, numObservations)
		storage.CheckGetObservations(t, shuffler.store, &key, batch.GetEncryptedObservation())
	}

	// clear store contents before testing a new envelope
	storage.ResetStoreForTesting(store, true)
}
