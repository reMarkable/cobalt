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
	"time"

	"github.com/golang/glog"
	"github.com/golang/protobuf/ptypes/empty"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials"

	"cobalt"
	"shuffler"
	"storage"
	"util"
)

var shufflerServerSingleton *ShufflerServer

// ShufflerServer implements the Shufffler service.
type ShufflerServer struct {
	store     storage.Store
	config    ServerConfig
	decrypter *util.MessageDecrypter
}

// ServerConfig specifies the configuration options for setting up a Grpc
// server.
type ServerConfig struct {
	// Connection uses TLS if true, else plain TCP
	EnableTLS bool
	// The TLS cert file
	CertFile string
	// The TLS key file
	KeyFile string
	// The server port
	Port int
	// A PEM encoding of the Shuffler's private key for use in Cobalt's custom
	// hybrid encryption scheme.
	// TODO(rudominer) Support key rotation: Rather than a single private key
	// this should be a set of (public-key-hash, private-key) pairs.
	PrivateKeyPem string
}

// Process processes the incoming encoder requests and persists them locally in
// a random order. During dispatching, the records get sent to Analyzer and
// deleted from Shuffler.
func (s *ShufflerServer) Process(ctx context.Context,
	encryptedMessage *cobalt.EncryptedMessage) (*empty.Empty, error) {
	glog.V(4).Infoln("Process() is invoked.")
	envelope, err := s.decryptEnvelope(encryptedMessage)
	if err != nil {
		return nil, err
	}

	// TODO(ukode): Some notes here for future development:
	// Check the recipient first. If the request is intended for another Shuffler
	// do not open the envelope and route it to the next Shuffler directly using
	// a forwarder thread. Forward the request to the next Shuffler in chain for
	// further processing. This will be implemented by queueing the request in
	// a channel that the forwarder can consume and dispatch to the next
	// Shuffler |envelope.RecipientUrl|.

	// Extract the Observation from the sealed envelope, save it in Shuffler
	// data store for dispatcher to consume and forward to Analyzer based on
	// some dispatch criteria. The data store shuffles the order of the
	// Observation before persisting.
	if err := s.store.AddAllObservations(envelope.GetBatch(), storage.GetDayIndexUtc(time.Now())); err != nil {
		return nil, err
	}

	glog.V(4).Infoln("Process() done, returning OK.")
	return &empty.Empty{}, nil
}

// Run serves incoming encoder requests and blocks forever unless a fatal error
// occurs in the network layer. Run is invoked by the main() function in
// shuffler_main and will result in a fatal error if invoked twice within the
// same process.
func Run(dataStore storage.Store, config *ServerConfig) {
	if dataStore == nil {
		glog.Fatal("Invalid data store handle, exiting.")
	}

	if config == nil {
		glog.Fatal("Invalid server config, exiting.")
	}

	if shufflerServerSingleton != nil {
		glog.Fatal("Run() must not be invoked twice, exiting.")
	}

	// Start shuffler service
	shufflerServerSingleton = &ShufflerServer{
		store:     dataStore,
		config:    *config,
		decrypter: util.NewMessageDecrypter(config.PrivateKeyPem),
	}
	shufflerServerSingleton.startServer()
}

// startServer sets up and starts the grpc server using configuration from
// |ShufflerServer.ServerConfig|.
func (s *ShufflerServer) startServer() {
	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", s.config.Port))
	if err != nil {
		glog.Error("Grpc: Error in accepting connections on port [", s.config.Port, "]:", err)
		return
	}
	var opts []grpc.ServerOption
	if s.config.EnableTLS {
		creds, err := credentials.NewServerTLSFromFile(s.config.CertFile, s.config.KeyFile)
		if err != nil {
			glog.Error("Grpc: Failed to generate credentials:", err)
			return
		}
		opts = []grpc.ServerOption{grpc.Creds(creds)}
	}

	grpcServer := grpc.NewServer(opts...)
	shuffler.RegisterShufflerServer(grpcServer, s)
	glog.Info("Shuffler is listening on port ", s.config.Port, "...")
	grpcServer.Serve(lis)
}

// decryptEnvelope decrypts the incoming EncryptedMessage and returns an Envelope or an error.
func (s *ShufflerServer) decryptEnvelope(encryptedMessage *cobalt.EncryptedMessage) (*cobalt.Envelope, error) {
	if s.decrypter == nil {
		return nil, grpc.Errorf(codes.Internal, "s.decrypter is nil")
	}
	envelope := new(cobalt.Envelope)
	if err := s.decrypter.DecryptMessage(encryptedMessage, envelope); err != nil {
		glog.Errorf("Decryption failed: %v", err)
		return nil, err
	}
	return envelope, nil
}
