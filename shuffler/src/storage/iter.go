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

import "shuffler"

// Iterator is used to iterate over a DB snapshot in successive calls.
type Iterator interface {
	// Next advances the iterator to the next entry and returns whether or not
	// the iterator is still valid. The Get() method may only be invoked on a
	// valid iterator. A newly obtained iterator starts before the first valid
	// entry so Next() must be invoked before Get().
	Next() bool

	// Get returns the current entry the Iterator is pointing to or an error if
	// the iterator is invalid.
	Get() (*shuffler.ObservationVal, error)

	// The iterator must be released after use, by calling Release method.
	Release() error
}
