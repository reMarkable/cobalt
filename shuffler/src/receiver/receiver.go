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

/*
Package implementing a simple gRPC server that performs unary RPC to
implement shuffler service whose definition can be found in
shuffler/shuffler.proto.

A shuffler listens to incoming requests from Encoders (end users),
strips the user metadata like IP-address, timestamps etc before buffering
them locally based on the metadata information provided in the request.
*/

package receiver

import (
	"fmt"
	"net"

	"github.com/golang/glog"
	"github.com/golang/protobuf/proto"
	"github.com/golang/protobuf/ptypes/empty"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"

	shufflerpb "cobalt"
	"storage"
	util "util"
)

var (
	store storage.Store
)

// ShufflerServer is used to implement shuffler.ShufflerServer.
type ShufflerServer struct{}

// Process() function processes the incoming encoder requests and stores them
// locally in a random order. During dispatch event, the records get sent to
// Analyzer and deleted from Shuffler.
func (s *ShufflerServer) Process(ctx context.Context,
	encryptedMessage *shufflerpb.EncryptedMessage) (*empty.Empty, error) {
	// TODO(ukode): Add impl for decrypting the sealed envelope.
	glog.V(2).Infoln("Function Process() is invoked.")
	pubKey := encryptedMessage.PubKey
	ciphertext := encryptedMessage.Ciphertext

	c := util.NoOpCrypter{}

	envelope := &shufflerpb.Envelope{}
	err := proto.Unmarshal(c.Decrypt(ciphertext, pubKey), envelope)
	if err != nil {
		return nil, fmt.Errorf("Error in unmarshalling ciphertext: %v", err)
	}

	// TODO(ukode): Some notes here for future development:
	// Check the recipient first. If the request is intended for another Shuffler
	// do not open the envelope and route it to the next Shuffler directly using
	// a forwarder thread. Forward the request to the next Shuffler in chain for
	// further processing. This will be implemented by queueing the request in
	// a channel that the forwarder can consume and dispatch to the next
	// Shuffler |envelope.RecipientUrl|.

	for _, batch := range envelope.GetBatch() {
		for _, encryptedObservation := range batch.GetEncryptedObservation() {
			if encryptedObservation == nil {
				return nil, fmt.Errorf("Received empty encrypted message for key [%v]", batch.GetMetaData())
			}

			// Extract the Observation from the sealed envelope, save it in Shuffler data
			// store for dispatcher to consume and forward to Analyzer based on some
			// dispatch criteria. The data store shuffles the order of the Observation
			// before persisting.
			if err := store.AddObservation(batch.GetMetaData(), storage.MakeObservationInfo(encryptedObservation)); err != nil {
				return nil, fmt.Errorf("Error in saving observation: %v", batch.GetMetaData())
			}
		}
	}

	glog.V(2).Infoln("Process() done, returning OK.")
	return &empty.Empty{}, nil
}

func newServer() *ShufflerServer {
	server := new(ShufflerServer)
	return server
}

func initializeDataStore(dataStore storage.Store) {
	if dataStore == nil {
		glog.Fatal("Invalid data store handle, exiting.")
	}

	// Initialize data store handle
	store = dataStore
}

// ReceiveAndStore serves incoming requests from encoders.
func ReceiveAndStore(tls bool, certFile string, keyFile string, port int, dataStore storage.Store) {
	initializeDataStore(dataStore)

	// Start the grpc receiver and start listening for requests from Encoders
	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", port))
	if err != nil {
		glog.V(2).Info("Grpc: Failed to accept connections:", err)
		return
	}
	var opts []grpc.ServerOption
	if tls {
		creds, err := credentials.NewServerTLSFromFile(certFile, keyFile)
		if err != nil {
			glog.V(2).Info("Grpc: Failed to generate credentials:", err)
			return
		}
		opts = []grpc.ServerOption{grpc.Creds(creds)}
	}
	glog.V(2).Info("Starting Shuffler:", err)
	grpcServer := grpc.NewServer(opts...)
	shufflerpb.RegisterShufflerServer(grpcServer, newServer())
	grpcServer.Serve(lis)
}
