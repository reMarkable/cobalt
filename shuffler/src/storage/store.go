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
	shufflerpb "cobalt"
	"context"
)

// Store is a generic Shuffler data store interface to store and retrieve data
// from a local in-memory or persistent data store. All the methods take context
// as an argument, that is propagated to backends for persistent data stores.
type Store interface {
	// AddObservation inserts an encrypted message for a given
	// ObservationMetadata.
	AddObservation(context.Context, shufflerpb.ObservationMetadata, *shufflerpb.EncryptedMessage) error

	// GetObservations retrieves the list of EncryptedMessages from the
	// shuffler datastore corresponding to a given ObservationMetadata key.
	GetObservations(context.Context, shufflerpb.ObservationMetadata) ([]*shufflerpb.EncryptedMessage, error)

	// GetKeys returns the list of unique ObservationMetadata keys stored
	// in shuffler datastore.
	GetKeys(context.Context) []shufflerpb.ObservationMetadata

	// EraseAll deletes both the ObservationMetadata key and all its
	// EncryptedMessages.
	EraseAll(context.Context, shufflerpb.ObservationMetadata) error
}
