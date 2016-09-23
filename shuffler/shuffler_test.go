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

package main

import (
	"math/rand"
	"reflect"
	"testing"
)

// This is a fake Analyzer object that just caches ciphertexts in the order in
// which they are received.  This lets us verify the output of a shuffler.
type MockAnalyzer struct {
	ciphertexts [][]byte
}

func (a *MockAnalyzer) send(ciphertexts [][]byte) {
	a.ciphertexts = append(a.ciphertexts, ciphertexts...)
}

// Create 10 ciphertexts (messages).
// Add them to the shuffler.
// Expect that the analyzer receives 10 messages.
// Expect them to arrive at a different order from how they were inserted into
// the shuffler.
func TestBasicShuffler(t *testing.T) {
	var msgs [][]byte
	num := 10
	anal := MockAnalyzer{}
	shuff := BasicShuffler{analyzer: &anal, batchsize: num}

	// Make test deterministic
	rand.Seed(1)

	// Create the input messages.
	for i := 0; i < num; i++ {
		msgs = append(msgs, []byte{byte(i)})
	}

	// Send them to the shuffler
	for _, v := range msgs {
		shuff.add(Policy{}, v)
	}

	// Check that all messages were sent
	if len(anal.ciphertexts) != num {
		t.Error("Some messages didn't get sent")
		return
	}

	// Check that basic shuffling occurred
	// TODO(bittau) define the acceptance criteria for how good a shuffle is
	// and assert them.
	if reflect.DeepEqual(anal.ciphertexts, msgs) {
		t.Error("No shuffling happened")
		return
	}
}
