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
	"crypto/elliptic"
	"crypto/rand"
	"io"
	"testing"
)

func TestSymmetricCipher(t *testing.T) {
	const nonceSize = 12

	// test for an invalid key with length != 16 bytes
	key := []byte("AES256Key")
	_, err := NewSymmetricCipher(key)
	if err == nil {
		t.Errorf("expected error for invalid key")
		return
	}

	// Use a 16 byte key to select AES-128
	key = []byte("AES256Key-16Char")
	c, err := NewSymmetricCipher(key)
	if err != nil {
		t.Errorf("Unable to initialize test SymmetricCipher: %v", err)
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

func TestHybridCipher(t *testing.T) {
	privateKey, publicKey, _, _, err := generateECKey()
	if err != nil {
		t.Errorf("%v", err)
	}

	hybridCipher := NewHybridCipher(privateKey, publicKey)

	// This is Shakespearean Sonnet number 110.
	plaintext := `
  Alas 'tis true, I have gone here and there,
  And made my self a motley to the view,
  Gored mine own thoughts, sold cheap what is most dear,
  Made old offences of affections new.

  Most true it is, that I have looked on truth
  Askance and strangely: but by all above,
  These blenches gave my heart another youth,
  And worse essays proved thee my best of love.

  Now all is done, have what shall have no end,
  Mine appetite I never more will grind
  On newer proof, to try an older friend,
  A god in love, to whom I am confined.

    Then give me welcome, next my heaven the best,
    Even to thy pure and most most loving breast.`
	ciphertext, err := hybridCipher.Encrypt([]byte(plaintext))
	if err != nil {
		t.Errorf("%v", err)
	}

	recoveredText, err := hybridCipher.Decrypt(ciphertext)
	if err != nil {
		t.Errorf("%v", err)
	}
	if string(recoveredText) != plaintext {
		t.Errorf("recoveredText=[%s]", string(recoveredText))
	}

	// Intentionally corrupt the ciphertext by omitting the first byte.
	corruptedCiphertext := ciphertext[1:]
	// Check that Decrypt() returns an error but does not crash.
	_, err = hybridCipher.Decrypt(corruptedCiphertext)
	if err == nil {
		t.Errorf("Expected an error.")
	}

	// Intentionally corrupt the ciphertext by prepending an extra zero byte.
	corruptedCiphertext = []byte{0}
	corruptedCiphertext = append(corruptedCiphertext, ciphertext...)
	// Check that Decrypt() returns an error but does not crash.
	_, err = hybridCipher.Decrypt(corruptedCiphertext)
	if err == nil {
		t.Errorf("Expected an error.")
	}

	// Intentionally corrupt the ciphertext by appending an extra zero byte.
	corruptedCiphertext = append(ciphertext, 0)
	// Check that Decrypt() returns an error but does not crash.
	_, err = hybridCipher.Decrypt(corruptedCiphertext)
	if err == nil {
		t.Errorf("Expected an error.")
	}
}

func TestMarshalUnmarshall(t *testing.T) {
	_, _, pubX, pubY, err := generateECKey()
	if err != nil {
		t.Errorf("%v", err)
	}

	// Test uncompressed
	uncompressedData := elliptic.Marshal(ellipticCurve, pubX, pubY)
	if uncompressedData == nil {
		t.Errorf("Marshal failed.")
	}

	x, y := Unmarshal(ellipticCurve, uncompressedData)
	if x.Cmp(pubX) != 0 {
		t.Errorf("x's don't match")
	}
	if y.Cmp(pubY) != 0 {
		t.Errorf("x's don't match")
	}

	// Test compressed
	compressedData := MarshalCompressed(ellipticCurve, pubX, pubY)
	if compressedData == nil {
		t.Errorf("Marshal failed.")
	}

	x, y = Unmarshal(ellipticCurve, compressedData)
	if x.Cmp(pubX) != 0 {
		t.Errorf("x's don't match")
	}
	if y.Cmp(pubY) != 0 {
		t.Errorf("x's don't match")
	}
}
