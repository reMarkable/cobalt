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
	cryptorand "crypto/rand"
	"fmt"
	"math/big"
	mathrand "math/rand"
	"sync"
)

// Random is an interface that provides utility functions for generating random
// bytes, and integers.
type Random interface {
	// RandomBytes returns the specified |num| bytes of random data from a uniform
	// distribution or error if the underlying source of entropy fails.
	RandomBytes(num uint32) ([]byte, error)

	// RandomUint63 returns a uniformly random, 63-bit integer in the range
	// [0, max). |max| must be in the range (0, 2^63). Fails with an error if
	// |max| is invalid or if the underlying source of entropy fails.
	RandomUint63(max uint64) (uint64, error)
}

// DeterministicRandom uses a deterministic PRNG to generate random values
// using the less secure "math/rand" library apis.
type DeterministicRandom struct {
	mu   sync.RWMutex
	rand *mathrand.Rand
}

// NewDeterministicRandom creates and seeds the Deterministic PRNG.
func NewDeterministicRandom(seed int64) *DeterministicRandom {
	return &DeterministicRandom{
		rand: mathrand.New(mathrand.NewSource(seed)),
	}
}

// RandomBytes returns specified |num| bytes of random data from a uniform
// distribution or error if the underlying source of entropy fails.
func (r *DeterministicRandom) RandomBytes(num uint32) ([]byte, error) {
	var bytes = make([]byte, num)
	r.mu.Lock()
	defer r.mu.Unlock()
	_, err := r.rand.Read(bytes)
	return bytes, err
}

// RandomUint63 returns a uniformly random, 63-bit integer in the range
// [0, max). |max| must be in the range (0, 2^63). Fails with an error if
// |max| is invalid or if the underlying source of entropy fails.
func (r *DeterministicRandom) RandomUint63(max uint64) (uint64, error) {
	if max <= 0 || max >= 1<<63 {
		return 0, fmt.Errorf("Invalid |max| value [%v]", max)
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	// Int63n returns a non-negative int64 pseudo-random number in [0,max).
	return uint64(r.rand.Int63n(int64(max))), nil
}

// SecureRandom generates non-deterministic random values using the secure
// crypto/rand PRNG.
type SecureRandom struct{}

// RandomBytes returns specified |num| bytes of random data from a uniform
// distribution or error if the underlying source of entropy fails.
func (r *SecureRandom) RandomBytes(num uint32) ([]byte, error) {
	var bytes = make([]byte, num)
	_, err := cryptorand.Read(bytes)

	return bytes, err
}

// RandomUint63 returns a uniformly random, 63-bit integer in the range
// [0, max). |max| must be in the range (0, 2^63). Fails with an error if
// |max| is invalid or if the underlying source of entropy fails.
func (r *SecureRandom) RandomUint63(max uint64) (uint64, error) {
	if max <= 0 || max >= 1<<63 {
		return 0, fmt.Errorf("Invalid |max| value [%v]", max)
	}

	var z big.Int
	z.SetUint64(max)
	nBig, err := cryptorand.Int(cryptorand.Reader, &z)
	if err != nil {
		return 0, err
	}
	return nBig.Uint64(), nil
}
