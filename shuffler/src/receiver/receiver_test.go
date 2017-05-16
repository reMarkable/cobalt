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
	"reflect"
	"testing"

	"github.com/golang/protobuf/proto"

	shufflerpb "cobalt"
	"storage"
	"util"
)

// makeEnvelope creates an Envelope containing |numBatches| ObservationBatches
// containing |numObservationsPerBatch| EncryptedMessages each containing
// random ciphertext bytes.
func makeEnvelope(numBatches int, numObservationsPerBatch int) *shufflerpb.Envelope {
	var batch []*shufflerpb.ObservationBatch
	for i := 1; i <= numBatches; i++ {
		batch = append(batch, &shufflerpb.ObservationBatch{
			MetaData:             storage.NewObservationMetaData(i),
			EncryptedObservation: storage.MakeRandomEncryptedMsgs(numObservationsPerBatch),
		})
	}

	return &shufflerpb.Envelope{Batch: batch}
}

// makeTestEnvelopes creates sample envelopes with different configuration for
// testing.
func makeTestEnvelopes() []*shufflerpb.Envelope {
	emptyEnvelope := &shufflerpb.Envelope{}
	envelopeWithOneObservation := makeEnvelope(1, 1)
	envelopeWithMultipleObservations := makeEnvelope(1, 7)
	envelopeWithHybridObservations := makeEnvelope(10, 5)
	return []*shufflerpb.Envelope{emptyEnvelope, envelopeWithOneObservation, envelopeWithMultipleObservations, envelopeWithHybridObservations}
}

func TestMemStoreShuffler(t *testing.T) {
	for _, envelope := range makeTestEnvelopes() {
		doTestProcess(t, envelope, storage.NewMemStore())
	}
}

func TestLevelDBShuffler(t *testing.T) {
	for _, envelope := range makeTestEnvelopes() {
		levelDBStore, err := storage.NewLevelDBStore("/tmp")
		if err != nil {
			t.Errorf("Failed to initialize leveldb store")
			return
		}
		doTestProcess(t, envelope, levelDBStore)
	}
}

func doTestProcess(t *testing.T, envelope *shufflerpb.Envelope, store storage.Store) {
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

	_, err = shuffler.Process(context.Background(), eMsg)

	if err != nil && !reflect.DeepEqual(envelope, &shufflerpb.Envelope{}) {
		t.Fatalf("Expected success, got error: %v", err)
	}

	for _, batch := range envelope.GetBatch() {
		numObservations := len(batch.GetEncryptedObservation())
		if numObservations == 0 {
			continue
		}

		om := batch.GetMetaData()
		storage.CheckNumObservations(t, shuffler.store, om, numObservations)
		storage.CheckGetObservations(t, shuffler.store, om, batch.GetEncryptedObservation())
	}

	// clear store contents before testing a new envelope
	storage.ResetStoreForTesting(store, true)
}
