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
	shufflerpb "cobalt"
	"fmt"
	"os"
	"runtime"
	"strings"
	"sync"

	"github.com/golang/glog"
	"github.com/golang/protobuf/proto"
	"github.com/syndtr/goleveldb/leveldb"
	"github.com/syndtr/goleveldb/leveldb/opt"
	leveldb_util "github.com/syndtr/goleveldb/leveldb/util"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
)

// LevelDBStore is an persistent store implementation of the Store interface.
type LevelDBStore struct {
	// Path to leveldb database folder
	dbDir string

	// Observation database consisting of ObservationVals as values for each
	// rowkey (|ObservationMetadata|_<random_identifier>).
	db *leveldb.DB

	// bucketSizes is a map from active buckets to their current sizes. A
	// "bucket" is a set of ObservationVals with a common ObservationMetadata.
	// The keys to this map are string representations of ObservationMetadata and
	// the values are the number of entries in |db| corresponding to that
	// ObservationMetadata. Note that a single bucket is represented by many rows
	// of |db|.
	bucketSizes map[string]uint64

	// mu is the global mutex that protects all elements of |bucketSizes| in-memory
	// map.
	mu sync.RWMutex
}

// NewLevelDBStore returns an implementation of store using LevelDB
// (https://github.com/google/leveldb).
func NewLevelDBStore(dbDirPath string) (*LevelDBStore, error) {
	db, err := leveldb.OpenFile(dbDirPath, nil)
	if err != nil {
		if db != nil {
			db.Close()
		}
		return nil, err
	}

	store := &LevelDBStore{
		dbDir:       dbDirPath,
		db:          db,
		bucketSizes: make(map[string]uint64),
	}
	if err := store.initialize(); err != nil {
		return nil, err
	}

	return store, nil
}

// initialize populates in-memory metadata_db map by parsing rows from existing
// leveldb store.
func (store *LevelDBStore) initialize() error {
	iter := store.db.NewIterator(nil, nil)
	for iter.Next() {
		dbKey := string(iter.Key())
		bKey, err := ExtractBKey(dbKey)
		if err != nil {
			glog.Errorln("Existing DB key [", dbKey, "] found corrupted: ", err)
			continue
		}
		store.bucketSizes[bKey]++
	}
	iter.Release()
	if err := iter.Error(); err != nil {
		return err
	}

	return nil
}

// close closes the database files and unlocks any resources used by
// leveldb.
func (store *LevelDBStore) close() error {
	if store.db != nil {
		if err := store.db.Close(); err != nil {
			return err
		}
		store.db = nil
	}
	runtime.GC()
	return nil
}

// rowKeyPrefix returns the leveldb |prefixRange| for the given
// ObservationMetadata |om| or an error. RowKey prefix is used in generating
// unique row keys and also as an index into |bucketSizes| map for LevelDBStore.
func rowKeyPrefix(om *shufflerpb.ObservationMetadata) (prefixRange *leveldb_util.Range, err error) {
	if om == nil {
		panic("Metadata is nil")
	}

	bKey, err := BKey(om)
	if err != nil {
		return nil, err
	}

	prefix := strings.Join([]string{bKey}, "_")

	return leveldb_util.BytesPrefix([]byte(prefix)), nil
}

// makeDBVal returns a serialized |ObservationVal| generated from the given
// |encryptedObservation|, |id| and |arrivalDayIndex|.
func makeDBVal(encryptedObservation *shufflerpb.EncryptedMessage, id string, arrivalDayIndex uint32) ([]byte, error) {
	if encryptedObservation == nil {
		panic("encryptedObservation is nil")
	}

	valBytes, err := proto.Marshal(NewObservationVal(encryptedObservation, id, arrivalDayIndex))
	if err != nil {
		return []byte(""), err
	}
	return valBytes, nil
}

// AddAllObservations adds all of the encrypted observations in all of the
// ObservationBatches in |envelopeBatch| to the store. New |ObservationVal|s
// are created to hold the values and the given |arrivalDayIndex|. Returns a
// non-nil error if the arguments are invalid or the operation fails.
func (store *LevelDBStore) AddAllObservations(envelopeBatch []*shufflerpb.ObservationBatch, arrivalDayIndex uint32) error {
	dbBatch := new(leveldb.Batch)

	tmpBucketSizes := make(map[string]uint64)

	// process all observations into a tmp |dbBatch|
	for _, batch := range envelopeBatch {
		if batch == nil {
			return grpc.Errorf(codes.InvalidArgument, "One of the ObservationBatches in the Envelope is not set.")
		}

		om := batch.GetMetaData()
		if om == nil {
			return grpc.Errorf(codes.InvalidArgument, "The meta_data field is unset for one of the ObservationBatches.")
		}

		bKey, err := BKey(om)
		if err != nil {
			return grpc.Errorf(codes.Internal, "Error in making bucket key for metadata [%v]: [%v]", om, err)
		}

		glog.V(3).Infoln(fmt.Sprintf("Received a batch of %d encrypted Observations.", len(batch.GetEncryptedObservation())))
		for _, encryptedObservation := range batch.GetEncryptedObservation() {
			if encryptedObservation == nil {
				return grpc.Errorf(codes.InvalidArgument, "One of the encrypted_observations in one of the ObservationBatches with metadata [%v] was null", om)
			}

			// generate a new random key for each encrypted observation
			key, id, err := NewRowKey(bKey)
			if err != nil {
				glog.Errorln("AddAllObservations() failed in generating PKey for metadata [", om, "]: ", err)
				return grpc.Errorf(codes.Internal, "Error in processing observation metadata for batch [%v]", om)
			}

			// generate |ObservationVal| for each encrypted observation
			val, err := makeDBVal(encryptedObservation, id, arrivalDayIndex)
			if err != nil {
				glog.Errorln("AddAllObservations() failed in parsing observation value for metadata [", *om, "]: ", err)
				return grpc.Errorf(codes.Internal, "Error in processing one of the observations for metadata [%v]", *om)
			}

			dbBatch.Put(key, val)
			tmpBucketSizes[bKey]++
		}
	}

	// Set db write options |Sync| to sync underlying writes from the OS buffer
	// cache through to actual disk immediately and |NoWriteMerge| to disable
	// write merge on concurrent access. Setting Sync can result in slower writes.
	// If same key is specified twice, it will get overwritten by the most recent
	// update.
	woptions := &opt.WriteOptions{
		NoWriteMerge: false,
		Sync:         true,
	}

	// commit |dbBatch|
	if err := store.db.Write(dbBatch, woptions); err != nil {
		glog.Errorln("AddAllObservations failed with error:", err)
		return grpc.Errorf(codes.Internal, "Internal error in processing the ObservationBatch.")
	}

	// update counts for all keys
	store.mu.Lock()
	defer store.mu.Unlock()
	for k := range tmpBucketSizes {
		store.bucketSizes[k] += tmpBucketSizes[k]
	}

	return nil
}

// GetObservations returns the shuffled list of ObservationVals from the
// data store for the given |ObservationMetadata| key or returns an error.
// TODO(ukode): If the returned resultset cannot fit in memory, the api
// needs to be tweaked to return ObservationVals in batches.
func (store *LevelDBStore) GetObservations(om *shufflerpb.ObservationMetadata) ([]*shufflerpb.ObservationVal, error) {
	if om == nil {
		panic("observation metadata is nil")
	}

	keyPrefix, err := rowKeyPrefix(om)
	if err != nil {
		return nil, grpc.Errorf(codes.InvalidArgument, "Error in generating rowkey prefix for observation metadata [%v]: [%v]", *om, err)
	}

	iter := store.db.NewIterator(keyPrefix, nil)
	var obVals []*shufflerpb.ObservationVal
	for iter.Next() {
		obVal := &shufflerpb.ObservationVal{}
		if err := proto.Unmarshal(iter.Value(), obVal); err != nil {
			return nil, grpc.Errorf(codes.Internal, "Error in parsing observation value from datastore: [%v]", err)
		}
		obVals = append(obVals, obVal)
	}
	iter.Release()
	if err := iter.Error(); err != nil {
		return nil, grpc.Errorf(codes.Internal, "LevelDB iterator error: [%v]", err)
	}

	return obVals, nil
}

// GetKeys returns the list of all |ObservationMetadata| keys stored in the
// data store or returns an error.
func (store *LevelDBStore) GetKeys() ([]*shufflerpb.ObservationMetadata, error) {
	store.mu.RLock()
	defer store.mu.RUnlock()

	keys := []*shufflerpb.ObservationMetadata{}
	for bKey := range store.bucketSizes {
		om, err := UnmarshalBKey(bKey)
		if err != nil {
			return nil, grpc.Errorf(codes.Internal, "Error in parsing observation metadata [%v]: [%v]", *om, err)
		}
		keys = append(keys, om)
	}
	return keys, nil
}

// DeleteValues deletes the given |ObservationVal|s for |ObservationMetadata|
// key from the data store or returns an error.
func (store *LevelDBStore) DeleteValues(om *shufflerpb.ObservationMetadata, obVals []*shufflerpb.ObservationVal) error {
	if om == nil {
		panic("observation metadata is nil")
	}

	if len(obVals) == 0 {
		return nil
	}

	batch := new(leveldb.Batch)
	for _, obVal := range obVals {
		rowKey, err := RowKeyFromMetadata(om, obVal.Id)
		if err != nil {
			return grpc.Errorf(codes.InvalidArgument, "Error in making rowkey from observation metadata [%v]: [%v]", om, err)
		}
		batch.Delete([]byte(rowKey))
	}

	if err := store.db.Write(batch, nil); err != nil {
		return grpc.Errorf(codes.Internal, "LevelDB write error: [%v]", err)
	}

	// update bucketSizes map for the deleted rows
	store.mu.Lock()
	defer store.mu.Unlock()
	bKey, err := BKey(om)
	if err != nil {
		return grpc.Errorf(codes.InvalidArgument, "Error in parsing observation metadata [%v]: [%v]", om, err)
	}
	store.bucketSizes[bKey] -= uint64(len(obVals))
	return nil
}

// GetNumObservations returns the total count of ObservationVals in the data
// store for the given |ObservationMmetadata| key or returns an error.
func (store *LevelDBStore) GetNumObservations(om *shufflerpb.ObservationMetadata) (int, error) {
	if om == nil {
		panic("observation metadata is nil")
	}

	bKey, err := BKey(om)
	if err != nil {
		return 0, grpc.Errorf(codes.InvalidArgument, "Error in parsing observation metadata [%v]: [%v]", om, err)
	}

	store.mu.RLock()
	defer store.mu.RUnlock()
	count, present := store.bucketSizes[bKey]
	if !present {
		return 0, grpc.Errorf(codes.InvalidArgument, "Observation metadata [%v] not found.", om)
	}

	return int(count), nil
}

// Reset clears any in-memory caches and deletes all data permanently from
// the |store| if |destroy| is set to true.
func (store *LevelDBStore) Reset(destroy bool) {
	// clear bucketSizes map
	store.mu.Lock()
	defer store.mu.Unlock()
	store.bucketSizes = make(map[string]uint64)

	// clear and reset db instance
	store.close()
	if destroy {
		os.RemoveAll(store.dbDir)
	}
}
