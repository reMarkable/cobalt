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
	"crypto/cipher"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha512"
	"fmt"
	"io"
	"math/big"

	// We need to import glog so that the flag --logtostderr is recognized since
	// our test infrastructure passes this flag to all tests.
	_ "github.com/golang/glog"

	"golang.org/x/crypto/hkdf"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
)

// symmetricCipherKeySize is the size in bytes of the key used by SymmetricCipher.
const symmetricCipherKeySize = 128 / 8

// symmetricCipherNonceSize is the size in bytes of the nonce used by SymmetricCipher.
const symmetricCipherNonceSize = 96 / 8

const hybridCipherSaltSize = 128 / 8 // Salt for HKDF

var allZeroNonce []byte

func init() {
	allZeroNonce = make([]byte, symmetricCipherNonceSize)
}

// SymmetricCipher implements an AEAD symmetric cipher.
type SymmetricCipher struct {
	// Underlying implementation. We use AES-128/GCM. If this changes the
	// numeric constants above must also change.
	aesgcm cipher.AEAD
}

// NewSymmetricCipher returns a new SymmetricCipher that uses the given |key|,
// or an error.
//
// The |key| must have length |symmetricCipherKeySize|.
func NewSymmetricCipher(key []byte) (*SymmetricCipher, error) {
	if len(key) != symmetricCipherKeySize {
		return nil, grpc.Errorf(codes.InvalidArgument, "key must be %d bytes", symmetricCipherKeySize)
	}

	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, grpc.Errorf(codes.Internal, "error constructing AES block cipher: %v", err)
	}

	aesgcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, grpc.Errorf(codes.Internal, "error constructing GCM: %v", err)
	}

	return &SymmetricCipher{
		aesgcm: aesgcm,
	}, nil
}

// Encrypt performs AEAD encryption on the |plaintext| using
// SymmetricCipher. |nonce| must have length |symmetricCipherNonceSize|. It is
// essential that the same (key, nonce) pair never be used to encrypt two
// different plain texts. If re-using the same key multiple times you *must*
// change the nonce or the resulting encryption will not be secure. Returns
// encrypted |ciphertext| on success or an error on failure.
//
// Panics if SymmetricCipher |c| is nil.
func (c *SymmetricCipher) Encrypt(plaintext []byte, nonce []byte) (ciphertext []byte, err error) {
	if c == nil {
		panic("SymmetricCipher is nil")
	}

	if c.aesgcm == nil {
		err = grpc.Errorf(codes.Internal, "SymmetricCipher is not initialized")
		return
	}

	if plaintext == nil {
		err = grpc.Errorf(codes.InvalidArgument, "plaintext is nil")
		return
	}

	if len(nonce) != symmetricCipherNonceSize {
		err = grpc.Errorf(codes.InvalidArgument, "nonce must be %d bytes", symmetricCipherNonceSize)
		return
	}

	ciphertext = c.aesgcm.Seal(nil, nonce, plaintext, nil)
	return
}

// Decrypt performs AEAD decryption on the given |ciphertext| using
// SymmetricCipher. |nonce| must have length |symmetricCipherNonceSize|.Returns
// decrypted |plaintext| on success or an error on failure.
//
// Panics if SymmetricCipher |c| is nil.
func (c *SymmetricCipher) Decrypt(ciphertext []byte, nonce []byte) (plaintext []byte, err error) {
	if c == nil {
		panic("SymmetricCipher is nil")
	}

	if c.aesgcm == nil {
		err = grpc.Errorf(codes.Internal, "SymmetricCipher is not initialized")
		return
	}

	if ciphertext == nil {
		err = grpc.Errorf(codes.InvalidArgument, "ciphertext is nil")
		return
	}

	if len(nonce) != symmetricCipherNonceSize {
		err = grpc.Errorf(codes.InvalidArgument, "nonce must be %d bytes", symmetricCipherNonceSize)
		return
	}

	plaintext, err = c.aesgcm.Open(nil, nonce, ciphertext, nil)
	return
}

// HybridCipher implements a public-key hybrid encryption scheme using ECIES-KEM.
//
// The following description of the algorithm is copied from the C++ implementation
// in util/crypto_util/cipher.h except that the use of the variables "x" and "y"
// there have been replaced by "alpha" and "beta" here. It would have been
// confusing to use "x" and "y" here since in the implementation we use
// "x" and "y" to refer to the coordinates of a point on an elliptic curve.
// Some other words have also been changed to make the description make sense
// in this context.
//
// Public key = g^alpha in an elliptic curve group (NIST P256) represented in
// X9.62 serialization with a compressed point representation.
//
// Private key = alpha stored in bytes and interpreted as a big-endian number.
//
// Enc(public key, message):
//
//    1. Samples a fresh EC keypair (g^beta, beta)
//    2. Samples a salt
//    3. Computes symmetric key = HKDF(g^beta, g^alpha*beta, salt) with SHA512
//    compression function.
//    4. (Symmetric) encrypts message using SymmetricCipher.Encrypt with key
//    and all-zero nonce into symmetric_ciphertext
//    5. Publishes (public_key_part, salt, symmetric_ciphertext) as the hybrid
//    ciphertext, where public_key_part is the X9.62 serialization of g^beta.
//
// Dec(private key, hybrid_ciphertext)
//    where hybrid_ciphertext = (public_key_part, salt, symmetric_ciphertext):
//
//    1. Computes symmetric key = HKDF(g^beta, g^(alpha*beta), salt) with SHA512
//    compression function from private key (alpha) and public_key_part (g^beta)
//    2. (Symmetric) decrypts symmetric_ciphertext using
//    SymmetricCipher::decrypt with key and all-zero nonce.
type HybridCipher struct {
	privateKey             []byte
	publicKeyX, publicKeyY *big.Int
}

// Returns a new HybridCipher. It may be used for encryption if |publicKey|
// is not nil and it may be used for decryption if |privateKey| is not nil.
func NewHybridCipher(privateKey, publicKey []byte) *HybridCipher {
	var publicX, publicY *big.Int
	if publicKey != nil {
		publicX, publicY = Unmarshal(ellipticCurve, publicKey)
	}
	return &HybridCipher{
		privateKey: privateKey,
		publicKeyX: publicX,
		publicKeyY: publicY,
	}
}

// generateECKey generates a new key pair of the form
// (privateKey, publicKey) = (alpha, g^alpha). Here g^alpha is an element of
// the elliptic curve group, g is a generator of the group, and alpha is
// an element of the underlying prime field.
//
// The private key alpha is returned in serialized form as |priv|. The public key
// is returned in two different forms. First it is returned in serialized form
// as |pub|. Second it is returned as the coordinates of the point g^alpha
// as |pubX|, |pubY|.
func generateECKey() (priv, pub []byte, pubX, pubY *big.Int, err error) {
	priv, pubX, pubY, err = elliptic.GenerateKey(ellipticCurve, rand.Reader)
	pub = MarshalCompressed(ellipticCurve, pubX, pubY)
	return
}

// deriveKey returns a key of size |symmetricCipherKeySize| derived from the given inputs.
// It invokes HKDF-sha512 using (|publicKeyPart|, |sharedKey|) as the master key
// and the given |salt|
func deriveKey(publicKeyPart, sharedKey, salt []byte) ([]byte, error) {
	// hkdfInput is the master key parameter to hkdf(). We use the concatenation
	// of the publicKeyPart and the sharedKey
	hkdfInput := make([]byte, len(publicKeyPart)+len(sharedKey))
	copy(hkdfInput, publicKeyPart)
	copy(hkdfInput[len(publicKeyPart):], sharedKey)

	hkdf := hkdf.New(sha512.New, hkdfInput, salt, nil)

	hkdfDerivedKey := make([]byte, symmetricCipherKeySize)
	n, err := io.ReadFull(hkdf, hkdfDerivedKey)
	if err != nil {
		return nil, err
	}
	if n != len(hkdfDerivedKey) {
		err = fmt.Errorf("n=%d but len(hkdfDerivedKey)=%d", n, len(hkdfDerivedKey))
		return nil, err
	}

	return hkdfDerivedKey, nil
}

func (c *HybridCipher) Encrypt(plaintext []byte) (hybridCiphertext []byte, err error) {
	if c.publicKeyX == nil {
		err = fmt.Errorf("The public key was not set")
		return
	}

	// Generate fresh EC key (privateKeyPart, publicKeyPart) = (beta, g^beta).
	privateKeyPart, publicKeyPart, _, _, err := generateECKey()

	// Compute the shared key g^{alpha*beta}
	sharedKey := computeSharedKey(c.publicKeyX, c.publicKeyY, privateKeyPart)

	// Generate a random salt
	salt := make([]byte, hybridCipherSaltSize)
	var n int
	n, err = io.ReadFull(rand.Reader, salt)
	if err != nil {
		return
	}
	if n != len(salt) {
		err = fmt.Errorf("n=%d but len(salt)=%d", n, len(salt))
		return
	}

	// Derive hkdfDerivedKey by running HKDF with SHA512 and the salt.
	hkdfDerivedKey, err := deriveKey(publicKeyPart, sharedKey, salt)
	if err != nil {
		return
	}

	// Do symmetric encryption with hkdfDerivedKey
	symmetricCipher, err := NewSymmetricCipher(hkdfDerivedKey)
	if err != nil {
		return
	}

	// For hybrid mode, we can fix the nonce to all zeroes without losing
	// security. See: https://goto.google.com/aes-gcm-zero-nonce-security
	symmetricCiphertext, err := symmetricCipher.Encrypt(plaintext, allZeroNonce)
	if err != nil {
		return
	}

	if len(publicKeyPart) != ecSerializationSize {
		panic(fmt.Sprintf("len(publicKeyPart)=%d", len(publicKeyPart)))
	}
	hybridCiphertext = make([]byte, len(publicKeyPart)+len(salt)+len(symmetricCiphertext))
	copy(hybridCiphertext, publicKeyPart)
	copy(hybridCiphertext[len(publicKeyPart):], salt)
	copy(hybridCiphertext[(len(publicKeyPart)+len(salt)):], symmetricCiphertext)
	return
}

func (c *HybridCipher) Decrypt(hybridCiphertext []byte) (plaintext []byte, err error) {
	if c.privateKey == nil {
		err = fmt.Errorf("The private key was not set")
		return
	}

	if len(hybridCiphertext) < ecSerializationSize+hybridCipherSaltSize+1 {
		err = fmt.Errorf("len(hybridCiphertext)=%d", len(hybridCiphertext))
		return
	}
	publicKeyPart := hybridCiphertext[:ecSerializationSize]
	salt := hybridCiphertext[ecSerializationSize : ecSerializationSize+hybridCipherSaltSize]
	symmetricCiphertext := hybridCiphertext[ecSerializationSize+hybridCipherSaltSize:]

	// The publicKeyPart is g^beta.
	publicX, publicY := Unmarshal(ellipticCurve, publicKeyPart)
	if publicX == nil || publicY == nil {
		err = fmt.Errorf("Unable to parse publicKeyPart as a group element.")
		return
	}
	// Compute sharedKey g^{alpha*beta}
	sharedKey := computeSharedKey(publicX, publicY, c.privateKey)

	// Derive hkdfDerivedKey by running HKDF with SHA512 and the salt.
	hkdfDerivedKey, err := deriveKey(publicKeyPart, sharedKey, salt)
	if err != nil {
		return
	}

	// Decrypt using symm_cipher_ interface
	symmetricCipher, err := NewSymmetricCipher(hkdfDerivedKey)
	if err != nil {
		return
	}
	plaintext, err = symmetricCipher.Decrypt(symmetricCiphertext, allZeroNonce)
	return
}
