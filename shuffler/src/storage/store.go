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

import shufflerpb "cobalt"

// ObservationInfo is the value stored in the Shuffler data store. It contains
// the sealed encrypted message blob and the timestamp when it was added to the
// store.
type ObservationInfo struct {
	creationTimestamp uint32
	encryptedMessage  *shufflerpb.EncryptedMessage
}

// Store is a generic Shuffler data store interface to store and retrieve data
// from a local in-memory or persistent data store. Data store contains
// ObservationMetadata as keys and the corresponding list of ObservationInfos
// as values.
type Store interface {
	// AddObservation inserts |observationInfo| into the data store under the key
	// |metadata|.
	AddObservation(metadata shufflerpb.ObservationMetadata, observationInfo *ObservationInfo) error

	// GetObservations retrieves the list of ObservationInfos from the data store
	// for the given |metadata| key.
	GetObservations(metadata shufflerpb.ObservationMetadata) ([]*ObservationInfo, error)

	// GetKeys returns the list of unique ObservationMetadata keys stored in the
	// data store.
	GetKeys() []shufflerpb.ObservationMetadata

	// EraseAll deletes both the |metadata| key and all it's ObservationInfos
	// from the data store.
	EraseAll(metadata shufflerpb.ObservationMetadata) error
}
