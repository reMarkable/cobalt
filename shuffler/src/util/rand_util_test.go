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
	"bytes"
	"reflect"
	"testing"
)

var (
	max = uint64(999) // upper limit of the random number range
	num = uint32(7)   // length of the random string generated
)

func TestDeterministicRandom(t *testing.T) {
	r := NewDeterministicRandom(int64(1))

	bytes1, err := r.RandomBytes(num)
	if err != nil {
		t.Errorf("got error [%v], want random bytes", err)
	}
	bytes1Str := string(bytes1)
	if bytes.Equal(bytes1, make([]byte, num)) {
		t.Errorf("got [%v], want random string", bytes1Str)
	}

	bytes2, err := r.RandomBytes(num)
	if err != nil {
		t.Errorf("got error [%v], want random bytes", err)
	}
	if bytes.Equal(bytes1, bytes2) {
		t.Errorf("got [%v], want a random string", bytes1Str)
	}

	if len(bytes1Str) != int(num) {
		t.Errorf("got string [%v], expecting a string of length [%d]", bytes1Str, num)
	}

	// invalid max
	for _, m := range []uint64{0, 1 << 63, 1<<63 + 1, 1<<63 + 100} {
		_, rErr := r.RandomUint63(m)
		if rErr == nil {
			t.Errorf("got success, expected error for [%v] max value", m)
		}
	}

	// valid max value
	n, err := r.RandomUint63(max)
	if err != nil {
		t.Errorf("got error [%v], want success", err)
	}
	if n >= max {
		t.Errorf("got [%d], want a random integer between [0] and [%d]", n, max)
	}

	// tests, if the same sequence of random numbers {864, 332, 464} are produced
	// with each test run using Determinstic PRNG with seed=1
	p, err := r.RandomUint63(max)
	if err != nil {
		t.Errorf("got error [%v], want success", err)
	}
	if n == p {
		t.Errorf("got [%d], expected randomness in next generated number", p)
	}

	q, err := r.RandomUint63(max)
	if err != nil {
		t.Errorf("got error [%v], want success", err)
	}

	if n == q {
		t.Errorf("got [%d], expected randomness in next generated number", q)
	}

	a := []uint64{864, 332, 464}
	b := []uint64{n, p, q}

	if !reflect.DeepEqual(a, b) {
		t.Errorf("got the sequence [%v], want sequence [%v] for a DetermisticRandom generator using seed=1", b, a)
	}
}

func TestSecureRandom(t *testing.T) {
	r := SecureRandom{}

	bytes1, err := r.RandomBytes(num)
	if err != nil {
		t.Errorf("got error [%v], want random bytes", err)
	}
	bytes1Str := string(bytes1)
	if bytes.Equal(bytes1, make([]byte, num)) {
		t.Errorf("got [%v], want a random string", bytes1Str)
	}

	bytes2, err := r.RandomBytes(num)
	if err != nil {
		t.Errorf("got error [%v], want random bytes", err)
	}
	if bytes.Equal(bytes1, bytes2) {
		t.Errorf("got [%v], expected a random string on every call", string(bytes2))
	}

	if len(bytes1Str) != int(num) {
		t.Errorf("got string [%v], expecting a string of length [%d]", bytes1Str, num)
	}

	// invalid max
	for _, m := range []uint64{0, 1 << 63, 1<<63 + 1, 1<<63 + 100} {
		_, rErr := r.RandomUint63(m)
		if rErr == nil {
			t.Errorf("got success, expected error for [%v] max value", m)
		}
	}

	// valid max
	n, err := r.RandomUint63(max)
	if err != nil {
		t.Errorf("got error [%v], want success", err)
	}
	if n >= max {
		t.Errorf("got [%d], want a random integer between [0] and [%d]", n, max)
	}

	// subsequent random number generation within the range
	p, err := r.RandomUint63(max)
	if err != nil {
		t.Errorf("got error [%v], want success", err)
	}

	if n == p {
		t.Errorf("got [%d], expected randomness in next generated number", p)
	}
}
