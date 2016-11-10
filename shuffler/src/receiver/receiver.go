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
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"

	cobaltpb "cobalt"
	"dispatcher"
	util "util"
)

// shufflerServer is used to implement shuffler.ShufflerServer.
type shufflerServer struct{}

// Process() function processes the incoming encoder requests and sends it to
// analyzer.
func (s *shufflerServer) Process(ctx context.Context,
	encryptedMessage *cobaltpb.EncryptedMessage) (*cobaltpb.ShufflerResponse, error) {
	// TODO(ukode): Add impl for decrypting the sealed envelope and then batch
	// and shuffle the payloads.
	glog.V(2).Infoln("Function Process() is invoked.")
	pubKey := encryptedMessage.PubKey
	ciphertext := encryptedMessage.Ciphertext

	c := util.NoOpCrypter{}

	envelope := &cobaltpb.Envelope{}
	err := proto.Unmarshal(c.Decrypt(ciphertext, pubKey), envelope)
	if err != nil {
		return nil, fmt.Errorf("Error in unmarshalling ciphertext: %v", err)
	}

	// TODO(ukode): Replace this test code that talks to analyzer by dispatching
	// instantaneously with real impl in the following cls.
	go dispatcher.Dispatch(envelope)

	// TODO(ukode): Replace ShufflerResponse with Empty proto.
	return &cobaltpb.ShufflerResponse{}, nil
}

func newServer() *shufflerServer {
	s := new(shufflerServer)
	return s
}

// ReceiveAndStore serves incoming requests from encoders.
func ReceiveAndStore(tls bool, certFile string, keyFile string, port int) {
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
	grpcServer := grpc.NewServer(opts...)
	cobaltpb.RegisterShufflerServer(grpcServer, newServer())
	grpcServer.Serve(lis)
}
