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
	"github.com/golang/protobuf/proto"
	leveldb_iter "github.com/syndtr/goleveldb/leveldb/iterator"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"

	"shuffler"
)

// LevelDBStoreIterator provides an iterator to parse the result set pointed to
// by an underlying leveldb iterator object.
type LevelDBStoreIterator struct {
	iter leveldb_iter.Iterator
}

// NewLevelDBStoreIterator builds and initializes a new |LevelDBStoreIterator|
// from the input \it|.
func NewLevelDBStoreIterator(it leveldb_iter.Iterator) Iterator {
	if it == nil {
		panic("LevelDBStore Iterator is nil.")
	}

	return &LevelDBStoreIterator{
		iter: it,
	}
}

// Get returns the current entry the Iterator is pointing to or an error if the
// iterator is invalid or the value is invalid.
func (li *LevelDBStoreIterator) Get() (*shuffler.ObservationVal, error) {
	if li == nil {
		panic("LevelDBStore Iterator is nil.")
	}

	if li.iter == nil {
		return nil, grpc.Errorf(codes.Internal, "Invalid iterator")
	}

	obVal := &shuffler.ObservationVal{}
	if err := proto.Unmarshal(li.iter.Value(), obVal); err != nil {
		return nil, grpc.Errorf(codes.Internal, "Error in parsing observation value from datastore: [%v]", err)
	}

	return obVal, nil
}

// Next advances the iterator to the next entry and returns whether or not
// the iterator is still valid. The Get() method may only be invoked on a
// valid iterator. A newly obtained iterator starts before the first valid
// entry so Next() must be invoked before Get().
func (li *LevelDBStoreIterator) Next() bool {
	if li == nil {
		panic("LevelDBStore Iterator is nil.")
	}

	if li.iter == nil {
		return false
	}

	return li.iter.Next()
}

// Release releases the iterator after use.
func (li *LevelDBStoreIterator) Release() error {
	if li == nil {
		panic("LevelDBStore Iterator is nil.")
	}

	li.iter.Release()
	if err := li.iter.Error(); err != nil {
		li.iter = nil
		return grpc.Errorf(codes.Internal, "LevelDB iterator error: [%v]", err)
	}

	li.iter = nil
	return nil
}
