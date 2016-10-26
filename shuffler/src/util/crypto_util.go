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

// Crypter interface provides functionality to encrypt and decryt text.
type Crypter interface {
	Encrypt(plainText []byte, key string) (cipherText []byte)
	Decrypt(cipherText []byte, key string) (plainText []byte)
}

// NoOpCrypter is a crypter interface that does nothing and is used for testing.
// TODO(ukode): Provide real impl
type NoOpCrypter struct{}

// Encrypt the encoded serialized msg to generate cipher text
func (c *NoOpCrypter) Encrypt(plainText []byte, key string) (cipherText []byte) {
	return plainText
}

// Decrypt the cipher text to encoded serialized msg
func (c *NoOpCrypter) Decrypt(cipherText []byte, key string) (plainText []byte) {
	return cipherText
}
