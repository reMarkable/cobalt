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
	"fmt"
	"time"

	"github.com/golang/glog"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials"

	"analyzer/analyzer_service"
	"cobalt"
	"shuffler"
	"storage"
	"util/stackdriver"
)

// We sleep for this amount of time between buckets and between batches within a bucket
const dispatchDelay = 1 * time.Second

// In the case that FrequencyInHours has been set to zero we sleep for this
// duration between each invocation of Dispatch().
const minWaitTime = 1 * time.Second

const (
	dispatchFailed              = "dispatcher-dispatch-failed"
	dispatchBucketFailed        = "dispatcher-dispatch-bucket-failed"
	deleteOldObservationsFailed = "dispatcher-delete-old-observations-failed"
	makeBatchFailed             = "dispatcher-make-batch-failed"
)

// AnalyzerTransport is an interface for Analyzer where the observations get
// collected, analyzed and reported.
type AnalyzerTransport interface {
	send(obBatch *cobalt.ObservationBatch) error
	close()
	connect() error
}

// GrpcClientConfig lists the grpc client configuration parameters required to
// connect to Analyzer.
//
// If |EnableTLS| is false an insecure connection is used, and the remaining
// parameters except |URL| are ignored, otherwise TLS is used.
//
// |cc.CAFile| is optional. If non-empty it should specify the path to a file
// containing a PEM encoding of root certificates to use for TLS.
//
// |URL| specifies the url for the analyzer.
//
// |Timeout| specifies the time duration to terminate the client
// grpc connection to analyzer.
type GrpcClientConfig struct {
	EnableTLS bool
	CAFile    string
	Timeout   time.Duration
	URL       string
}

// GrpcAnalyzerTransport sends data to Analyzer specified by Grpc |clientConfig|
// using the |client| interface.
//
// |conn| handle is used for closing and re-establishing grpc connections when
// dispatcher toggles between send and wait modes.
type GrpcAnalyzerTransport struct {
	clientConfig *GrpcClientConfig
	conn         *grpc.ClientConn
	client       analyzer_service.AnalyzerClient
}

// NewGrpcAnalyzerTransport establishes a Grpc connection to the Analyzer
// specified by |clientConfig|, and returns a new |GrpcAnalyzerTransport|.
//
// Panics if |clientConfig| is nil or if the underlying grpc connection cannot
// be established.
func NewGrpcAnalyzerTransport(clientConfig *GrpcClientConfig) *GrpcAnalyzerTransport {
	transport := GrpcAnalyzerTransport{
		clientConfig: clientConfig,
	}
	err := transport.connect()
	if err != nil {
		glog.Fatalf("Unable to establish initial connection to the Analyzer: %v", err)
	}
	return &transport
}

// connect attempts to establish a connection to the analyzer endpoint using
// the configuration specified in |g|'s |client_config| and panics if it is not
// set.
//
// If |EnableTLS| is false an insecure connection is used, and the remaining
// parameters or ignored, otherwise TLS is used.
//
// |CAFile| is optional. If non-empty it should specify the path to a file
// containing a PEM encoding of root certificates to use for TLS.
//
// Returns a non-nil error on failure.
func (g *GrpcAnalyzerTransport) connect() (err error) {
	if g.clientConfig == nil {
		panic("clientConfig is not set.")
	}

	glog.V(3).Infoln("Connecting to analyzer at:", g.clientConfig.URL)
	var opts []grpc.DialOption
	if g.clientConfig.EnableTLS {
		var creds credentials.TransportCredentials
		if g.clientConfig.CAFile != "" {
			var err error
			creds, err = credentials.NewClientTLSFromFile(g.clientConfig.CAFile, "")
			if err != nil {
				return grpc.Errorf(codes.Internal, "Failed to create TLS credentials %v", err)
			}
		} else {
			creds = credentials.NewClientTLSFromCert(nil, "")
		}
		opts = append(opts, grpc.WithTransportCredentials(creds))
	} else {
		opts = append(opts, grpc.WithInsecure())
	}
	opts = append(opts, grpc.WithBlock())
	opts = append(opts, grpc.WithTimeout(g.clientConfig.Timeout))

	glog.V(4).Infoln("Dialing", g.clientConfig.URL, "...")
	g.conn, err = grpc.Dial(g.clientConfig.URL, opts...)
	if err != nil {
		return grpc.Errorf(codes.Internal, "Error in establishing connection to Analyzer [%v]: %v", g.clientConfig.URL, err)
	}

	g.client = analyzer_service.NewAnalyzerClient(g.conn)

	return nil
}

// close closes all the grpc underlying connections to Analyzer.
func (g *GrpcAnalyzerTransport) close() {
	if g.conn != nil {
		g.conn.Close()
	}
	g.conn = nil
	g.client = nil
}

// shouldRetry returns true just in case the gRPC status code embedded in |err|
// indicates a failure for which retrying is appropriate.
func shouldRetry(err error) bool {
	// Note that a switch statement in Go does not fall through.
	switch grpc.Code(err) {
	case codes.Aborted:
	case codes.Canceled:
	case codes.DeadlineExceeded:
	case codes.Internal:
	case codes.Unavailable:
	default:
		return false
	}
	return true
}

// shouldReconnect returns true just in case the gRPC status code embedded in
// |err| indiates a failure for which breaking and re-establishing the
// connection to the server may be appropriate. We are basing this on
// empirical evidence. If the Analyzer Service restarts but the Shuffler
// does not restart then sometimes the Shuffler gets into a state where
// it's connection to the analyzer is invalid and the Go gRPC library is
// unable to recover. We are working around this by reconnecting.
// See issue CB-132.
func shouldReconnect(err error) bool {
	switch grpc.Code(err) {
	case codes.Internal:
		return true
	}
	return false
}

// sendToAnalyzer sends |obBatch| using the given AnalyzerTransport. It
// implements a simple retry and reconnect logic: In case of a send failure,
// depending on the returned error code, it may try up to |numAttempts| times
// with a sleep between attempts of |sleepMillis| ms. Also depending on the
// error code it may disconnect and reconnect.
func sendToAnalyzer(t AnalyzerTransport, obBatch *cobalt.ObservationBatch,
	numAttempts int, sleepMillis int) (err error) {
	// We implement a simple-minded retry strategy: Try a few times with a
	// few seconds wait in between attempts. We don't bother with exponential
	// backoff or jitter or anything else fancy. This strategy is sufficient
	// given that if the send fails then in the next iteration of the Shuffler's
	// Run() loop it will attempt to send all unsent observations.
	for i := 0; i < numAttempts; i++ {
		err = t.send(obBatch)
		if err == nil || i == (numAttempts-1) || !shouldRetry(err) {
			return err
		}
		if shouldReconnect(err) {
			t.close()
			err = t.connect()
			if err != nil {
				glog.Errorf("Unable to reestablish a connection to the Analyzer: %v", err)
			}
		}
		glog.V(3).Infof("send attempt failed. Sleeping for %v milliseconds", sleepMillis)
		time.Sleep(time.Duration(sleepMillis) * time.Millisecond)
	}
	// Control never reaches this point
	return nil
}

// send forwards a given ObservationBatch to Analyzer using the AddObservations
// interface.
func (g *GrpcAnalyzerTransport) send(obBatch *cobalt.ObservationBatch) error {
	if g == nil {
		panic("g is nil.")
	}

	if obBatch == nil {
		return grpc.Errorf(codes.InvalidArgument, "ObservationBatch is not set.")
	}

	if g.conn == nil || g.client == nil {
		return grpc.Errorf(codes.Internal, "Cannot send: Not currently connected to Analyzer")
	}

	// Shuffler forwards a new context, so as to break the context correlation
	// between originating request and the shuffled request that is being
	// forwarded.
	glog.V(3).Infof("sending batch of %d observations to the analyzer.", len(obBatch.GetEncryptedObservation()))
	_, err := g.client.AddObservations(context.Background(), obBatch)
	if err != nil {
		glog.Errorf("AddObservations call failed with error: %v", err)
		return err
	}

	glog.V(4).Infoln("ObservationBatch dispatched successfully.")
	return nil
}

// Dispatcher stores and forwards encoder requests to |analyzer|s based on the
// type of |store|, |config|, |batchSize| and the |lastDispatchTime|.
type Dispatcher struct {
	store             storage.Store
	config            *shuffler.ShufflerConfig
	batchSize         int
	analyzerTransport AnalyzerTransport
	lastDispatchTime  time.Time
}

var dispatcherSingleton *Dispatcher

// Start function either routes the incoming request from Encoder to next
// Shuffler or to the Analyzer, if the dispatch criteria is met. If the
// dispatch criteria is not met, the incoming Observation is buffered locally
// for the next dispatch attempt.
func Start(config *shuffler.ShufflerConfig, store storage.Store, batchSize int, analyzerTransport AnalyzerTransport) {
	if store == nil {
		glog.Fatal("Invalid data store handle, exiting.")
	}

	if config == nil {
		glog.Fatal("Invalid server config, exiting.")
	}

	if analyzerTransport == nil {
		glog.Fatal("Invalid Analyzer client.")
	}

	if batchSize <= 0 {
		glog.Fatal("Invalid batch size.")
	}

	if dispatcherSingleton != nil {
		glog.Fatal("Start() must not be invoked twice, exiting.")
	}

	// invoke dispatcher
	dispatcherSingleton := &Dispatcher{
		store:             store,
		config:            config,
		batchSize:         batchSize,
		analyzerTransport: analyzerTransport,
		lastDispatchTime:  time.Time{},
	}
	dispatcherSingleton.Run()
}

// Run dispatches stored observations to the Analyzer per each
// ObservationMetadata key if threshold and dispatch frequency are met. If the
// criteria is not met, dispatcher goes back to wait mode until the next
// dispatch attempt.
//
// The underlying grpc connection to analyzer is closed when the dispatcher
// goes to sleep mode.
func (d *Dispatcher) Run() {
	for {
		waitTime := d.computeWaitTime(time.Now())
		shouldDisconnectWhileSleeping := true
		if waitTime <= minWaitTime {
			waitTime = minWaitTime
			// Don't bother disconnecting and reconnecting for a 3 second sleep.
			shouldDisconnectWhileSleeping = false
		}
		if shouldDisconnectWhileSleeping {
			glog.V(3).Infoln("Close existing connection to Analyzer...")
			d.analyzerTransport.close()
		}

		glog.V(5).Infof("Dispatcher sleeping for [%v]...", waitTime)
		time.Sleep(waitTime)

		if shouldDisconnectWhileSleeping {
			glog.V(3).Infoln("Re-establish grpc connection to Analyzer before the next dispatch...")
			err := d.analyzerTransport.connect()
			if err != nil {
				glog.Errorf("Unable to reconnect to the Analyzer: %v", err)
				break
			}
		}

		d.lastDispatchTime = time.Now()
		d.dispatch(dispatchDelay)
	}
}

// Dispatch sends encoded observations to the Analyzer based on the following
// criteria:
// 1. The dispatch interval between attempts should be atleast
//    |frequency_in_hours| as specified in the Shuffler configuration.
// 2. If frequency is met, Shuffler sends |ObservationBatch| to the Analyzer for
//    each |ObservationMetadata| key if and only if:
//    - The batch contains atleast |threshold| number of Observations, and
//    - For each eligible batch, the Observations in that batch will be
//      dispatched to the Analyzer and deleted from the Shuffler, and
//    - For each batch whose Observations are not dispatched to the Analyzer
//      because the batch size is too small, the Shuffler will delete those
//      Observations from the batch whose age is at least |disposal_age_days|
//      specified in the configuration.
//
// Between between buckets, and between the batches of a single bucket, we sleep
// for |sleepDuration|.
func (d *Dispatcher) dispatch(sleepDuration time.Duration) {
	if d.store == nil {
		panic("Store handle is nil.")
	}

	if d.config == nil {
		panic("Shuffler config is nil.")
	}

	glog.V(5).Infoln("Start dispatching ...")
	keys, err := d.store.GetKeys()
	if err != nil {
		stackdriver.LogCountMetricf(dispatchFailed, "GetKeys() failed with error: %v", err)
		return
	}

	// Each bucket is either dispatched or disposed based on config and if there
	// are errors, processing proceeds to the next bucket in the pipeline.
	for _, key := range keys {
		// Fetch bucket size for each key.
		//
		// We use the value returned from GetNumObservations() to determine whether
		// or not to dispatch a bucket. But it's important to note that this value
		// is not necessarily exactly equal to the number of Observations in the
		// Store. This is because new Observations are being added to the store and
		// the the count is being incremented asynchronously with this method and
		// non-transactionally. In particular note that it is possible that the
		// value returned from GetNumObservations() may, temporarily, be negative.
		// We do maintain the following invariant: Let n = the value returned from
		// GetNumObservations(). Then an invocation of GetObservations() by this
		// same thread immediately afterwards will find at least n Observations.
		// (The reason this invariant holds is that this is the only thread that
		// ever deletes from the store or decrements the count. All other threads
		// first add to the store, commit, and then increment the count.) This
		// allows us to use the result of GetNumObservations() for conservative
		// thresholding: We will not dispatch a bucket unless GetNumObservations()
		// returns a value at least as large as the threshold.
		bucketSize, err := d.store.GetNumObservations(key)
		glog.V(5).Infof("Bucket size from store: [%d]", bucketSize)
		if err != nil {
			stackdriver.LogCountMetricf(dispatchFailed, "GetNumObservations() failed for key: %v with error: %v", key, err)
			continue
		}

		// Compare bucket size to the configured limit.
		if uint32(bucketSize) >= d.config.GetGlobalConfig().Threshold {
			// Dispatch bucket associated with |key| and delete it after sending.
			err := d.dispatchBucket(key, sleepDuration)
			if err != nil {
				stackdriver.LogCountMetricf(dispatchFailed, "dispatchBucket() failed for key: %v with error: %v", key, err)
				continue
			}
		} else {
			// If threshold policy is not met, loop through the messages and check
			// if any messages are in the queue for more than the allowed duration
			// |disposal_age_days|. If found, discard them, otherwise queue it back
			// in the store for the next dispatch event.
			err = d.deleteOldObservations(key, storage.GetDayIndexUtc(time.Now()), d.config.GetGlobalConfig().DisposalAgeDays)
			if err != nil {
				stackdriver.LogCountMetricf(dispatchFailed, "Error in filtering Observations for key [%v]: %v", key, err)
			}
		}
		time.Sleep(sleepDuration)
	}
}

// dispatchBucket dispatches the ObservationBatch associated with |key| in
// chunks of size |batchSize| to Analyzer using grpc transport.
//
// We sleep for |sleepDuration| between batches.
func (d *Dispatcher) dispatchBucket(key *cobalt.ObservationMetadata, sleepDuration time.Duration) error {
	if key == nil {
		panic("key is nil")
	}

	if d == nil {
		panic("dispatcher is nil")
	}

	// Retrieve shuffled bucket from store for the given |key|
	iterator, err := d.store.GetObservations(key)
	if err != nil {
		stackdriver.LogCountMetricf(dispatchBucketFailed, "GetObservations() failed for key: %v with error: %v", key, err)
		return err
	}

	// send the shuffled bucket to Analyzer in chunks. If the bucket is too
	// big, send it in multiple chunks of size |batchSize|.
	batchID := 0
	for {
		batchID++
		glog.V(4).Infof("sending observations to Analyzer in chunks, batch [%d] in progress...", batchID)
		obVals, batchTosend := makeBatch(key, iterator, d.batchSize)
		if len(obVals) == 0 {
			// If makeBatch() returned an empty batch then the iteration is done.
			break
		}
		sendErr := sendToAnalyzer(d.analyzerTransport, batchTosend, 4, 2500)
		if sendErr == nil {
			// After successful send, delete the observations from the local
			// datastore.
			if err := d.store.DeleteValues(key, obVals); err != nil {
				stackdriver.LogCountMetricf(dispatchBucketFailed, "Error in deleting dispatched observations from the store for key: %v", key)
			}
		} else {
			stackdriver.LogCountMetricf(dispatchBucketFailed, "Error in transmitting data to Analyzer for key [%v]: %v", key, sendErr)
		}
		time.Sleep(sleepDuration)
	}

	return nil
}

// deleteOldObservations deletes the observations for a given |key| from the
// store if the age of the observation is greater than the configured value
// |disposalAgeInDays|.
func (d *Dispatcher) deleteOldObservations(key *cobalt.ObservationMetadata,
	currentDayIndex uint32, disposalAgeInDays uint32) error {
	if key == nil {
		panic("key is nil")
	}

	if d == nil {
		panic("dispatcher is nil")
	}

	iterator, err := d.store.GetObservations(key)
	if err != nil {
		stackdriver.LogCountMetricf(deleteOldObservationsFailed, "GetObservation call failed for key: %v with error: %v", key, err)
		return nil
	}

	// We delete stale Observations iteratively in batches of size at most 1000.
	const maxDeleteBatchSize = 1000
	for {
		var staleObVals []*shuffler.ObservationVal
		for iterator.Next() {
			obVal, err := iterator.Get()
			if err != nil {
				stackdriver.LogCountMetricf(deleteOldObservationsFailed, "deleteOldObservations: iterator.Get() returned an error: %v", err)
				continue
			}
			if currentDayIndex-obVal.ArrivalDayIndex > disposalAgeInDays {
				staleObVals = append(staleObVals, obVal)
				if len(staleObVals) == maxDeleteBatchSize {
					break
				}
			}
		}

		if len(staleObVals) == 0 {
			break
		} else if err := d.store.DeleteValues(key, staleObVals); err != nil {
			return fmt.Errorf("Error [%v] in deleting old observations for metadata: %v", err, key)
		}
	}

	return nil
}

// computeWaitTime returns the Duration until the next dispatch should occur.
// Note that this may be negative.
func (d *Dispatcher) computeWaitTime(currentTime time.Time) (waitTime time.Duration) {
	if d == nil {
		panic("Dispatcher is not set")
	}

	dispatchInterval := time.Duration(d.config.GetGlobalConfig().FrequencyInHours) * time.Hour
	nextDispatchTime := d.lastDispatchTime.Add(dispatchInterval)
	return nextDispatchTime.Sub(currentTime)
}

// makeBatch returns a new ObservationBatch for |key| consisting of the next
// chunk of observations from |iterator| of size at most |batchSize|.
func makeBatch(key *cobalt.ObservationMetadata, iterator storage.Iterator, batchSize int) ([]*shuffler.ObservationVal, *cobalt.ObservationBatch) {
	if batchSize <= 0 {
		panic("batchSize must be positive.")
	}

	var encryptedMessages []*cobalt.EncryptedMessage
	var obVals []*shuffler.ObservationVal
	for iterator.Next() {
		obVal, err := iterator.Get()
		if err != nil {
			stackdriver.LogCountMetricf(makeBatchFailed, "makeBatch: iterator.Get() returned an error: %v", err)
			continue
		}
		obVals = append(obVals, obVal)
		encryptedMessages = append(encryptedMessages, obVal.EncryptedObservation)
		if len(encryptedMessages) == batchSize {
			break
		}
	}

	batch := cobalt.ObservationBatch{
		MetaData:             key,
		EncryptedObservation: encryptedMessages,
	}

	return obVals, &batch
}
