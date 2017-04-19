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
	"crypto/aes"
	cipher "crypto/cipher"

	// We need to import glog so that the flag --logtostderr is recognized since
	// our test infrastructre passes this flag to all tests.
	_ "github.com/golang/glog"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
)

// keySize is the allowed length in bytes for AES key in SymmetricCipherCrypter
// interface to perform 128-bit encryption operations.
const keySize = 16

// nonceSize is the standard nonce size in bytes to perform 128-bit encryption
// operations in SymmetricCipherCrypter interface.
const nonceSize = 12

// Crypter interface provides functionality to encrypt and decrypt text.
type Crypter interface {
	Encrypt(plaintext []byte, nonce []byte) (ciphertext []byte, err error)
	Decrypt(ciphertext []byte, nonce []byte) (plaintext []byte, err error)
}

// SymmetricCipherCrypter implements the Crypter interface using AEAD. AEAD is
// a cipher mode providing authenticated encryption with associated data.
//
// An instance of SymmetricCipher may be used repeatedly for multiple
// encryptions or decryptions.
type SymmetricCipherCrypter struct {
	// Underlying implementation of AES-128 GCM providing authenticated encryption
	// with associated data.
	aesgcm cipher.AEAD
}

// NewSymmetricCipherCrypter returns a new SymmetricCipherCrypter using AES-128
// GCM encryption scheme. If success, the returned SymmetricCipherCrypter
// contains a block cipher wrapped in Galois Counter Mode with the standard
// nonce length. Otherwise, returns an error if the length of |key| is not
// equal to |keySize| or if the underlying block cipher cannot be generated.
func NewSymmetricCipherCrypter(key []byte) (*SymmetricCipherCrypter, error) {
	if len(key) != keySize {
		return nil, grpc.Errorf(codes.InvalidArgument, "key must be %d bytes", keySize)
	}

	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, grpc.Errorf(codes.Internal, "error in generating AES block cipher: %v", err)
	}

	aesgcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, grpc.Errorf(codes.Internal, "error in generating GCM block cipher: %v", err)
	}

	return &SymmetricCipherCrypter{
		aesgcm: aesgcm,
	}, nil
}

// Encrypt performs AEAD encryption on the |plaintext| using
// SymmetricCipherCrypter. |nonce| must have length |nonceSize|. It is
// essential that the same (key, nonce) pair never be used to encrypt two
// different plain texts. If re-using the same key multiple times you *must*
// change the nonce or the resulting encryption will not be secure. Returns
// encrypted |ciphertext| on success or an error on failure.
//
// Panics if SymmetricCipherCrypter |c| is nil.
func (c *SymmetricCipherCrypter) Encrypt(plaintext []byte, nonce []byte) (ciphertext []byte, err error) {
	if c == nil {
		panic("SymmetricCipherCrypter is nil")
	}

	if c.aesgcm == nil {
		err = grpc.Errorf(codes.Internal, "SymmetricCipherCrypter is not initialized")
		return
	}

	if plaintext == nil {
		err = grpc.Errorf(codes.InvalidArgument, "plaintext is nil")
		return
	}

	if len(nonce) != nonceSize {
		err = grpc.Errorf(codes.InvalidArgument, "nonce must be %d bytes", nonceSize)
		return
	}

	ciphertext = c.aesgcm.Seal(nil, nonce, plaintext, nil)
	return
}

// Decrypt performs AEAD decryption on the given |ciphertext| using
// SymmetricCipherCrypter. |nonce| must have length |nonceSize|.Returns
// decrypted |plaintext| on success or an error on failure.
//
// Panics if SymmetricCipherCrypter |c| is nil.
func (c *SymmetricCipherCrypter) Decrypt(ciphertext []byte, nonce []byte) (plaintext []byte, err error) {
	if c == nil {
		panic("SymmetricCipherCrypter is nil")
	}

	if c.aesgcm == nil {
		err = grpc.Errorf(codes.Internal, "SymmetricCipherCrypter is not initialized")
		return
	}

	if ciphertext == nil {
		err = grpc.Errorf(codes.InvalidArgument, "ciphertext is nil")
		return
	}

	if len(nonce) != nonceSize {
		err = grpc.Errorf(codes.InvalidArgument, "nonce must be %d bytes", nonceSize)
		return
	}

	plaintext, err = c.aesgcm.Open(nil, nonce, ciphertext, nil)
	return
}
