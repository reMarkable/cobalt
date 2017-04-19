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

package util

import (
	"crypto/rand"
	"io"
	"testing"
)

func TestSymmetricCipherCrypter(t *testing.T) {
	const nonceSize = 12

	// test for an invalid key with length != 16 bytes
	key := []byte("AES256Key")
	_, err := NewSymmetricCipherCrypter(key)
	if err == nil {
		t.Errorf("expected error for invalid key")
		return
	}

	// Use a 16 byte key to select AES-128
	key = []byte("AES256Key-16Char")
	c, err := NewSymmetricCipherCrypter(key)
	if err != nil {
		t.Errorf("Unable to initialize test SymmetricCipherCrypter: %v", err)
		return
	}

	// test for different plaintexts
	for _, plaintextSize := range []int{32, 128, 256, 1024} {
		plaintext := make([]byte, plaintextSize)
		if _, err := io.ReadFull(rand.Reader, plaintext); err != nil {
			t.Errorf("got error in generating plaintext: %v", err)
			return
		}

		// generate random nonce
		nonce := make([]byte, nonceSize)
		if _, err := io.ReadFull(rand.Reader, nonce); err != nil {
			t.Errorf("got error in generating nonce: %v", err)
			return
		}

		// test encrypt
		ciphertext, err := c.Encrypt(plaintext, nonce)
		if err != nil {
			t.Errorf("got encryption error:%v", err)
			return
		}
		if ciphertext == nil {
			t.Error("ciphertext after encryption is nil")
			return
		}

		// test decrypt
		decryptedtext, err := c.Decrypt(ciphertext, nonce)
		if err != nil {
			t.Errorf("got decryption error:%v", err)
			return
		}
		if string(plaintext) != string(decryptedtext) {
			t.Errorf("got [%s] after decryption, want [%s]", decryptedtext, plaintext)
			return
		}
	}
}
