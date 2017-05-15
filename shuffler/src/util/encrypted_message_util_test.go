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
	"reflect"
	"testing"

	"cobalt"
)

var privateKeyPem, publicKeyPem string

func init() {
	privateKeyPem = `-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQg1kZxvT81qrRWg2Y8
g/M7YNtiHaC14/fbevhy/hgXcByhRANCAASkbLO+7iLLaPayYIr3YVmY0jkbwalG
sOB9Tf3R8TR7Ow43cHlGjX3HALV1z4Lxs1v2K13yeegBJF8lU88cdAqY
-----END PRIVATE KEY-----`

	publicKeyPem = `-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEpGyzvu4iy2j2smCK92FZmNI5G8Gp
RrDgfU390fE0ezsON3B5Ro19xwC1dc+C8bNb9itd8nnoASRfJVPPHHQKmA==
-----END PUBLIC KEY-----`
}

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
	if encryptedMessageMaker == nil {
		t.Fatal("Failed to create EncryptedMessageMaker")
	}

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
	// Make an EncryptedMessageMaker
	encryptedMessageMaker := NewEncryptedMessageMaker(publicKeyPem, cobalt.EncryptedMessage_HYBRID_ECDH_V1)
	if encryptedMessageMaker == nil {
		t.Fatal("Failed to create EncryptedMessageMaker")
	}

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
	// Make an EncryptedMessageMaker
	encryptedMessageMaker := NewEncryptedMessageMaker(publicKeyPem, cobalt.EncryptedMessage_HYBRID_ECDH_V1)
	if encryptedMessageMaker == nil {
		t.Fatal("Failed to create EncryptedMessageMaker")
	}

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
	// Make an EncryptedMessageMaker
	encryptedMessageMaker := NewEncryptedMessageMaker(publicKeyPem, cobalt.EncryptedMessage_HYBRID_ECDH_V1)
	if encryptedMessageMaker == nil {
		t.Fatal("Failed to create EncryptedMessageMaker")
	}

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
