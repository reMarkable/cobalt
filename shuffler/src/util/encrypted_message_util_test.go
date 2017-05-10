// Copyright 2017 The Fuchsia Authors
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

package util

import (
	"encoding/pem"
	"reflect"
	"testing"

	"cobalt"
)

// Makes and returns an Envelope with some non-default values so that we can recognized
// when we correctly encrypt and decrypt it.
func MakeTestEnvelope() cobalt.Envelope {
	return cobalt.Envelope{
		Batch: []*cobalt.ObservationBatch{
			&cobalt.ObservationBatch{
				MetaData: &cobalt.ObservationMetadata{
					CustomerId: 42,
					ProjectId:  43,
					MetricId:   44,
				},
			},
		},
	}
}

// Tests that a MessageDecrypter that is constructed without a valid private
// key can decrypt messages that use the NONE scheme.
func TestNoEncryption(t *testing.T) {
	// Make an EncryptedMessageMaker
	encryptedMessageMaker := NewEncryptedMessageMaker("", cobalt.EncryptedMessage_NONE)

	// Make an Envelope with some non-default values so we can recognize it.
	envelope1 := MakeTestEnvelope()

	// Encrypt the Envelope
	encryptedMessage, err := encryptedMessageMaker.Encrypt(&envelope1)
	if err != nil {
		t.Errorf("%v", err)
	}

	// Make a MessageDecrypter
	messageDecrypter := NewMessageDecrypter("")

	// Decrypt the Envelope
	envelope2 := cobalt.Envelope{}
	err = messageDecrypter.DecryptMessage(encryptedMessage, &envelope2)
	if err != nil {
		t.Errorf("%v", err)
	}

	// Compare the recovered envelope with the plaintext envelope
	if !reflect.DeepEqual(&envelope1, &envelope2) {
		t.Errorf("%v != %v", envelope1, envelope2)
	}
}

// Tests that a MessageDecrypter that is constructed with a valid private key
// can decrypt messages that use the HYBRID_ECDH_V1 scheme.
func TestHybridEncryption(t *testing.T) {
	// Generate a key pair.
	privateKey, publicKey, _, _, err := generateECKey()
	if err != nil {
		t.Errorf("%v", err)
	}

	// Make privateKeyPem
	block := pem.Block{
		Bytes: privateKey,
	}
	privateKeyPem := string(pem.EncodeToMemory(&block))

	// Make publicKeyPem
	block.Bytes = publicKey
	publicKeyPem := string(pem.EncodeToMemory(&block))

	// Make an EncryptedMessageMaker
	encryptedMessageMaker := NewEncryptedMessageMaker(publicKeyPem, cobalt.EncryptedMessage_HYBRID_ECDH_V1)

	// Make an Envelope with some non-default values so we can recognize it.
	envelope1 := MakeTestEnvelope()

	// Encrypt the Envelope
	encryptedMessage, err := encryptedMessageMaker.Encrypt(&envelope1)
	if err != nil {
		t.Errorf("%v", err)
	}

	// Make a MessageDecrypter
	messageDecrypter := NewMessageDecrypter(privateKeyPem)

	// Decrypt the Envelope
	envelope2 := cobalt.Envelope{}
	err = messageDecrypter.DecryptMessage(encryptedMessage, &envelope2)
	if err != nil {
		t.Errorf("%v", err)
	}

	// Compare the recovered envelope with the plaintext envelope
	if !reflect.DeepEqual(&envelope1, &envelope2) {
		t.Errorf("%v != %v", envelope1, envelope2)
	}
}

// Tests that a MessageDecrypter that is constructed with an invalid private key
// can fail gracefully.
func TestFailedHybridEncryption(t *testing.T) {
	// Generate a key pair.
	_, publicKey, _, _, err := generateECKey()
	if err != nil {
		t.Errorf("%v", err)
	}

	// Make publicKeyPem
	block := pem.Block{
		Bytes: publicKey,
	}
	publicKeyPem := string(pem.EncodeToMemory(&block))

	// Make an EncryptedMessageMaker
	encryptedMessageMaker := NewEncryptedMessageMaker(publicKeyPem, cobalt.EncryptedMessage_HYBRID_ECDH_V1)

	// Make an Envelope with some non-default values so we can recognize it.
	envelope1 := MakeTestEnvelope()

	// Encrypt the Envelope
	encryptedMessage, err := encryptedMessageMaker.Encrypt(&envelope1)
	if err != nil {
		t.Errorf("%v", err)
	}

	// Make a MessageDecrypter, but do not give it a valid privatekeyPem.
	messageDecrypter := NewMessageDecrypter("")

	// Try to decrypt the Envelope
	envelope2 := cobalt.Envelope{}
	err = messageDecrypter.DecryptMessage(encryptedMessage, &envelope2)
	if err == nil {
		t.Errorf("Expected an error.")
	}
}

// Tests that a MessageDecrypter that is constructed with a valid private key
// and is given a corrupted ciphertext can fail gracefully.
func TestCorruptedHybridEncryption(t *testing.T) {
	// Generate a key pair.
	privateKey, publicKey, _, _, err := generateECKey()
	if err != nil {
		t.Errorf("%v", err)
	}

	// Make privateKeyPem
	block := pem.Block{
		Bytes: privateKey,
	}
	privateKeyPem := string(pem.EncodeToMemory(&block))

	// Make publicKeyPem
	block.Bytes = publicKey
	publicKeyPem := string(pem.EncodeToMemory(&block))

	// Make an EncryptedMessageMaker
	encryptedMessageMaker := NewEncryptedMessageMaker(publicKeyPem, cobalt.EncryptedMessage_HYBRID_ECDH_V1)

	// Make an Envelope with some non-default values so we can recognize it.
	envelope1 := MakeTestEnvelope()

	// Encrypt the Envelope
	encryptedMessage, err := encryptedMessageMaker.Encrypt(&envelope1)
	if err != nil {
		t.Errorf("%v", err)
	}

	// Make a MessageDecrypter
	messageDecrypter := NewMessageDecrypter(privateKeyPem)

	// Corrupt the EncrptedMessage
	encryptedMessage.Ciphertext[0] = encryptedMessage.Ciphertext[0] + 1

	// Attempt to decrypt the Envelope
	envelope2 := cobalt.Envelope{}
	err = messageDecrypter.DecryptMessage(encryptedMessage, &envelope2)

	// Check that an error was returned, but there was no crash.
	if err == nil {
		t.Errorf("Expected an error.")
	}
}
