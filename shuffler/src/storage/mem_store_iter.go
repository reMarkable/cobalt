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

package storage

import (
	"fmt"
	"shuffler"
)

// MemStoreIterator implements the Iterator interface for MemStore.
type MemStoreIterator struct {
	obVals  []*shuffler.ObservationVal
	current int
}

// NewMemStoreIterator builds and initializes a new MemStoreIterator with the
// input |obVals|, and the current index set to -1.
func NewMemStoreIterator(obVals []*shuffler.ObservationVal) Iterator {
	if obVals == nil {
		panic("ObservationVals is nil")
	}

	return &MemStoreIterator{
		obVals:  obVals,
		current: -1,
	}
}

// Get returns the current entry the Iterator is pointing to or an error if the
// iterator is invalid.
func (mi *MemStoreIterator) Get() (*shuffler.ObservationVal, error) {
	if mi == nil {
		panic("MemStoreIterator is nil")
	}

	if mi.current < 0 || mi.current >= len(mi.obVals) {
		mi.Release()
		return nil, fmt.Errorf("Invalid iterator.")
	}

	return mi.obVals[mi.current], nil
}

// Next advances the iterator to the next entry and returns whether or not
// the iterator is still valid. The Get() method may only be invoked on a
// valid iterator. A newly obtained iterator starts before the first valid
// entry so Next() must be invoked before Get().
func (mi *MemStoreIterator) Next() bool {
	if mi == nil {
		panic("MemStoreIterator is nil")
	}

	mi.current++
	return mi.current < len(mi.obVals)
}

// Release releases the iterator after use.
func (mi *MemStoreIterator) Release() error {
	if mi == nil {
		panic("MemStoreIterator is nil")
	}

	mi.obVals = nil
	mi.current = -1

	return nil
}
