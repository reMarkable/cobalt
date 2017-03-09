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
	"crypto/rand"
	"reflect"
	"testing"

	"github.com/golang/protobuf/proto"

	shufflerpb "cobalt"
	"storage"
	"util"
)

func generateEnvelope(pubKey string, numBatches int) *shufflerpb.Envelope {
	var bytes = make([]byte, 20)
	// TODO(ukode): Return encrypted envelopes once PKE library is integrated.
	var eMsgList []*shufflerpb.EncryptedMessage
	for i := 0; i < numBatches; i++ {
		rand.Read(bytes)
		eMsgList = append(eMsgList, &shufflerpb.EncryptedMessage{
			Scheme:     shufflerpb.EncryptedMessage_NONE,
			PubKey:     pubKey,
			Ciphertext: bytes,
		})
	}
	return &shufflerpb.Envelope{
		Batch: []*shufflerpb.ObservationBatch{
			&shufflerpb.ObservationBatch{
				MetaData: &shufflerpb.ObservationMetadata{
					CustomerId: uint32(2),
					ProjectId:  uint32(22),
					MetricId:   uint32(222),
					DayIndex:   uint32(3),
				},
				EncryptedObservation: eMsgList,
			},
		},
	}
}

func generateEnvelopeForHybridObservations(pubKey string) *shufflerpb.Envelope {
	var bytes = make([]byte, 20)
	var batch []*shufflerpb.ObservationBatch
	// TODO(ukode): Return encrypted envelopes once PKE library is integrated.
	for i := 1; i <= 10; i++ {
		var eMsgList []*shufflerpb.EncryptedMessage
		for j := 0; j < 5; j++ {
			rand.Read(bytes)
			eMsgList = append(eMsgList, &shufflerpb.EncryptedMessage{
				Scheme:     shufflerpb.EncryptedMessage_NONE,
				PubKey:     pubKey,
				Ciphertext: bytes,
			})
		}

		batch = append(batch, &shufflerpb.ObservationBatch{
			MetaData: &shufflerpb.ObservationMetadata{
				CustomerId: uint32(i + 10),
				ProjectId:  uint32(i + 100),
				MetricId:   uint32(i + 1000),
				DayIndex:   uint32(i % 7),
			},
			EncryptedObservation: eMsgList,
		})
	}

	return &shufflerpb.Envelope{Batch: batch}
}

func TestProcess(t *testing.T) {
	testShuffler := ShufflerServer{}
	store := storage.NewMemStore()
	initializeDataStore(store)

	pubKey := "test_pub_key_1"
	emptyEnvelope := &shufflerpb.Envelope{}
	singleEnvelope := generateEnvelope(pubKey, 0)
	multiEnvelope := generateEnvelope(pubKey, 7)
	hybridEnvelope := generateEnvelopeForHybridObservations(pubKey)
	testEnvelopes := []*shufflerpb.Envelope{emptyEnvelope, singleEnvelope, multiEnvelope, hybridEnvelope}

	for _, envelope := range testEnvelopes {
		data, err := proto.Marshal(envelope)
		if err != nil {
			t.Fatalf("Error in marshalling envelope data: %v", err)
		}
		c := util.NoOpCrypter{}
		eMsg := &shufflerpb.EncryptedMessage{
			Scheme:     shufflerpb.EncryptedMessage_NONE,
			PubKey:     pubKey,
			Ciphertext: c.Encrypt(data, pubKey),
		}

		_, err = testShuffler.Process(context.Background(), eMsg)
		if err != nil {
			t.Fatalf("Expected success, got error: %v", err)
		}

		var om *shufflerpb.ObservationMetadata
		for _, batch := range envelope.GetBatch() {
			om = batch.GetMetaData()
			numObservations := len(batch.GetEncryptedObservation())
			if obInfoSize, sErr := store.GetNumObservations(om); sErr != nil {
				if numObservations != 0 {
					t.Errorf("got error %v, expected some saved observations", sErr)
				}
			} else {
				if obInfoSize != numObservations {
					t.Errorf("got observation count [%d], expected only one observation", obInfoSize)
				}
			}

			var gotObInfos []*storage.ObservationInfo
			if gotObInfos, err = store.GetObservations(om); err != nil {
				if numObservations != 0 {
					t.Errorf("got error %v, expected atleast one observation", err)
				}
			}

			// check the store if each observation was successfully saved
			for _, encryptedObservation := range batch.GetEncryptedObservation() {
				var found bool
				found = false
				for i := 0; i < numObservations; i++ {
					if reflect.DeepEqual(gotObInfos[i].EncryptedMessage, encryptedObservation) {
						found = true
						break
					}
				}

				if !found {
					t.Errorf("Observation not saved by Shuffler [%v]", encryptedObservation)
				}
			}
		}
	}
}
