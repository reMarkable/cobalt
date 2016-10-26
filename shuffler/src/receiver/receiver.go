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
them locally based on the manifest information provided in the request.
*/

package receiver

import (
	"fmt"
	"net"

	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/grpclog"

	spb "cobalt"
)

// shufflerServer is used to implement shuffler.ShufflerServer.
type shufflerServer struct{}

// Process service saves the incoming request to a local database after removing
// user identifiable fields such as IP addresses, timestamps etc and returns an
// empty response along with an error code.
func (s *shufflerServer) Process(ctx context.Context, encrypted_message *spb.EncryptedMessage) (*spb.ShufflerResponse, error) {
	// TODO(ukode): Add impl for decrypting the sealed envelope and then batch
	// and shuffle the payloads.
	return &spb.ShufflerResponse{}, nil
}

func newServer() *shufflerServer {
	s := new(shufflerServer)
	return s
}

// Process incoming requests from encoders and save them locally.
func ReceiveAndStore(tls bool, certFile string, keyFile string, port int) {
	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", port))
	if err != nil {
		grpclog.Fatalf("failed to listen: %v", err)
	}
	var opts []grpc.ServerOption
	if tls {
		creds, err := credentials.NewServerTLSFromFile(certFile, keyFile)
		if err != nil {
			grpclog.Fatalf("Failed to generate credentials %v", err)
		}
		opts = []grpc.ServerOption{grpc.Creds(creds)}
	}
	grpcServer := grpc.NewServer(opts...)
	spb.RegisterShufflerServer(grpcServer, newServer())
	grpcServer.Serve(lis)
}
