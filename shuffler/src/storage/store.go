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

package storage

import (
	"time"

	"cobalt"
	"shuffler"
)

// Store is a generic Shuffler data store interface to store and retrieve data
// from a local in-memory or persistent data store. Data store contains
// |ObservationMetadata| as keys and the corresponding list of |ObservationVal|
// as values.
type Store interface {
	// AddAllObservations adds all of the encrypted observations in all of the
	// ObservationBatches in |envelopeBatch| to the store. New |ObservationVal|s
	// are created to hold the values and the given |arrivalDayIndex|. Returns a
	// non-nil error if the arguments are invalid or the operation fails.
	AddAllObservations(envelopeBatch []*cobalt.ObservationBatch, arrivalDayIndex uint32) error

	// GetObservations returns a *shuffled* list of ObservationVals from the
	// data store for the given |ObservationMetadata| key or returns an error.
	// TODO(ukode): If the returned resultset cannot fit in memory, the api
	// needs to be tweaked to return ObservationVals in batches.
	GetObservations(metadata *cobalt.ObservationMetadata) ([]*shuffler.ObservationVal, error)

	// GetNumObservations returns the total count of ObservationVals in the data
	// store for the given |ObservationMmetadata| key or returns an error.
	GetNumObservations(metadata *cobalt.ObservationMetadata) (int, error)

	// GetKeys returns the list of all |ObservationMetadata| keys stored in the
	// data store or returns an error.
	GetKeys() ([]*cobalt.ObservationMetadata, error)

	// DeleteValues deletes the given |ObservationVal|s for |ObservationMetadata|
	// key from the data store or returns an error.
	DeleteValues(metadata *cobalt.ObservationMetadata, obVals []*shuffler.ObservationVal) error
}

// GetDayIndexUtc returns the day_index corresponding to the given Time |t|
// in the UTC time zone.
func GetDayIndexUtc(t time.Time) uint32 {
	epochTime := time.Date(1970, time.January, 1, 0, 0, 0, 0, time.UTC)
	return uint32(t.Sub(epochTime).Hours() / 24)
}

// NewObservationVal constructs an ObservationVal from the given
// |encryptedMessage|, |arrivalDayIndex| and |id| which should be a unique
// identifier for the new |ObservationVal|. Panics if |encryptedMessage|
// is nil.
func NewObservationVal(encryptedMessage *cobalt.EncryptedMessage, id string, arrivalDayIndex uint32) *shuffler.ObservationVal {
	if encryptedMessage == nil {
		panic("invalid encrypted message")
	}

	return &shuffler.ObservationVal{
		Id:                   id,
		ArrivalDayIndex:      arrivalDayIndex,
		EncryptedObservation: encryptedMessage,
	}
}
