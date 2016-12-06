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

// Shuffler receives ciphertexts from Encoders (end users), buffers them
// according to a policy, and then batch sends them in a random order to an
// Analyzer. The purpose is to break linkability between end users and
// ciphertexts from the Analyzer's point of view. The Analyzer does not know
// which end user produced which ciphertext.

package dispatcher

import (
	"errors"
	"fmt"
	"time"

	"github.com/golang/glog"
	"golang.org/x/net/context"
	"google.golang.org/grpc"

	shufflerpb "cobalt"
	"storage"
)

// Analyzer where the observations get collected, analyzed and reported.
type Analyzer interface {
	send(obBatch *shufflerpb.ObservationBatch) error
}

// GrpcAnalyzer sends data to Analyzer specified by |URL| using Grpc transport.
type GrpcAnalyzer struct {
	URL string
}

var lastTime time.Time

// Dispatch function either routes the incoming request from Encoder to next
// Shuffler or to the Analyzer, if the dispatch criteria is met. If the
// dispatch criteria is not met, the incoming Observation is buffered locally
// for the next dispatch attempt.
func Dispatch(config *shufflerpb.ShufflerConfig, store storage.Store, batchSize int, analyzer Analyzer) {
	lastTime = time.Now()

	if config == nil {
		glog.Fatal("Invalid config handle")
	}

	if store == nil {
		glog.Fatal("Invalid datastore handle")
	}

	if analyzer == nil {
		glog.Fatal("Invalid Analyzer handle")
	}

	if batchSize == 0 {
		glog.Fatal("Invalid batch size")
	}

	for {
		dispatchInternal(config, store, batchSize, analyzer)
	}
}

// Shuffler dispatches data to the Analyzer based on time and volume as
// follows:
// 1. The dispatch interval between attempts should be atleast
//    |frequency_in_hours| as specified in the Shuffler configuration.
// 2. If eligible, Shuffler sends |ObservationBatch| to the Analyzer for
//    each |ObservationMetadata| key only if:
//    - The batch contains atleast |threshold| number of Observations, and
//    - For each eligible batch, the Observations in that batch will be
//      dispatched to the Analyzer and deleted from the Shuffler, and
//    - For each batch whose Observations are not dispatched to the Analyzer
//      because the batch size is too small, the Shuffler will delete those
//      Observations from the batch whose age is at least
//      |disposal_age_days| specified in the configuration.
func dispatchInternal(config *shufflerpb.ShufflerConfig, store storage.Store, batchSize int, analyzer Analyzer) {
	dispatchIntervalInHours := config.GetGlobalConfig().FrequencyInHours
	glog.V(2).Infoln("Dispatch interval from config: [%d]", dispatchIntervalInHours)
	if !readyToDispatch(dispatchIntervalInHours, &lastTime) {
		time.Sleep(time.Duration(dispatchIntervalInHours*3600) * time.Second)
	}

	// Send ObservationBatch per key separately to Analyzer
	glog.V(2).Infoln("Start dispatching...")
	for _, key := range store.GetKeys() {
		// Get list of Observations for each key.
		bucketSize, err := store.GetNumObservations(key)
		glog.V(2).Infoln("Bucket size from store: [%d]", bucketSize)
		if err != nil {
			glog.V(2).Infoln(fmt.Sprintf("GetNumObservations call failed for key: %v with error: %v", key, err))
			return
		}

		// Compare bucket size to the configured limit.
		if uint32(bucketSize) >= config.GetGlobalConfig().Threshold {
			// Send the shuffled bucket to Analyzer in chunks. If the bucket is too
			// big, send it in multiple chunks of size |batchSize|.
			obInfos, err := store.GetObservations(key)
			glog.V(2).Infoln("Threshold met, processing further...")
			if err != nil {
				glog.V(2).Infoln(fmt.Sprintf("GetObservation call failed for key: %v with error: %v", key, err))
				return
			}

			var batchID int
			for i := 0; i < bucketSize; i += batchSize {
				batchID++
				var end int
				if (i + batchSize) <= bucketSize {
					end = i + batchSize
				} else {
					end = bucketSize
				}
				glog.V(2).Infoln("Sending observations to Analyzer in chunks, batch [%d] in progress...", batchID)
				err = analyzer.send(getObservationBatchChunk(key, obInfos, i, end))
				if err != nil {
					// TODO(ukode): Add retry behaviour for 3 or more attempts or use
					// exponential backoff for errors relating to network issues.
					glog.V(2).Infoln(fmt.Sprintf("Error in transmitting data to Analyzer for key [%v]: %v", key, err))
					return
				}

				// After successful send, remove the Observations from the local
				// datastore.
				if err := store.EraseAll(key); err != nil {
					glog.Fatal(fmt.Sprintf("Error in deleting dispatched observations from the store for key: %v", key))
				}
			}
		} else {
			// If threshold policy is not met, loop through the messages and check
			// if any messages are in the queue for more than the allowed duration
			// |dispage_age_days|. If found, discard them, otherwise queue it back
			// in the store for the next dispatch event.
			obInfos, err := store.GetObservations(key)
			if err != nil {
				glog.V(2).Infoln(fmt.Sprintf("GetObservation call failed for key: %v with error: %v", key, err))
				return
			}

			err = updateObservations(store, key, obInfos, config.GetGlobalConfig().DisposalAgeDays)
			if err != nil {
				glog.V(2).Infoln(fmt.Sprintf("Error in filtering Observations for key [%v]: %v", key, err))
				return
			}
		}
	}
}

// readyToDispatch determines if the Shuffler is eligible for sending
// requests to the Analyzer based on the |frequency_in_hours| interval
// configuration between the dispatch attempts.
func readyToDispatch(frequencyInHours uint32, lastTime *time.Time) bool {
	if lastTime == nil {
		return false
	}

	now := time.Now()
	elapsedHours := uint32(now.Sub(*lastTime).Hours())
	glog.V(2).Infoln(fmt.Sprintf("Dispatch time interval: %v", elapsedHours))
	if elapsedHours < frequencyInHours {
		return false
	}
	// lastTime is updated both at system startup and when the time elapsed
	// between the dispatch attempts is greater than |frequencyInHours|.
	*lastTime = now
	return true
}

// getObservationBatchChunk returns the chunk of Observations starting from
// index |start| to index |end| from the given list.
func getObservationBatchChunk(key *shufflerpb.ObservationMetadata, obInfos []*storage.ObservationInfo, start int, end int) *shufflerpb.ObservationBatch {
	var encryptedMessages []*shufflerpb.EncryptedMessage
	for i := start; i < end; i++ {
		encryptedMessages = append(encryptedMessages, obInfos[i].EncryptedMessage)
	}

	return &shufflerpb.ObservationBatch{
		MetaData:             key,
		EncryptedObservation: encryptedMessages,
	}
}

// updateObservations resets the Observations list based on the age of the
// Observation, and persists the valid ones back to the dispatch queue.
func updateObservations(store storage.Store, key *shufflerpb.ObservationMetadata, obInfos []*storage.ObservationInfo, disposalAgeInDays uint32) error {
	now := time.Now()
	var filteredObInfos []*storage.ObservationInfo
	for _, obInfo := range obInfos {
		if uint32(now.Sub(obInfo.CreationTimestamp).Hours()) < disposalAgeInDays*24 {
			filteredObInfos = append(filteredObInfos, obInfo)
		}
	}
	// TODO(ukode): Add an optimized version for filtering Observations.
	if err := store.EraseAll(key); err != nil {
		return fmt.Errorf("Error in removing unfiltered observations: %v", key)
	}
	for _, filteredObInfo := range filteredObInfos {
		if err := store.AddObservation(key, filteredObInfo); err != nil {
			return fmt.Errorf("Error in saving filtered observations: %v", key)
		}
	}
	return nil
}

// forwardToAnalyzer sends Observations for a given ObservationMetadata key to
// Analyzer using the AddObservations grpc call.
func (a *GrpcAnalyzer) send(obBatch *shufflerpb.ObservationBatch) error {
	if a.URL == "" {
		return errors.New("Invalid analyzer")
	}

	if obBatch == nil {
		return errors.New("Empty ObservationBatch")
	}

	glog.V(2).Infoln("Connecting to recipient: %s", a.URL)
	conn, err := grpc.Dial(a.URL, grpc.WithInsecure(), grpc.WithTimeout(time.Second*30))

	if err != nil {
		return fmt.Errorf("Connection failed for: %v with error: %v", a.URL, err)
	}

	defer conn.Close()
	c := shufflerpb.NewAnalyzerClient(conn)

	// Analyzer forwards a new context, so as to break the context correlation
	// between originating request and the shuffled request that is being
	// forwarded.
	_, err = c.AddObservations(context.Background(), obBatch)

	if err != nil {
		return fmt.Errorf("AddObservations call failed with error: %v", err)
	}

	glog.V(2).Infoln("Shuffler to Analyzer grpc call executed successfully")
	return nil
}
