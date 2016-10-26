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
	"testing"
)

var (
	plainText  = []byte("plain text")
	cipherText = []byte("cipher text")
	key        = "test_key"
)

func TestEncryptAndDecrypt(t *testing.T) {
	c := NoOpCrypter{}

	if string(c.Encrypt(plainText, key)) != string(plainText) {
		t.Error("Encryption error")
		return
	}

	if string(cipherText) != string(c.Decrypt(cipherText, key)) {
		t.Error("Decryption error")
		return
	}
}
