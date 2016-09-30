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

// Test client implements a simple gRPC client that performs unary RPC to
// shuffler service whose definition can be found in
// shuffler/service/shuffler.proto. This test could be used as an end to end
// test for testing functionality between encoders and shufflers. A shuffler
// processes the incoming request and returns an empty response, if successful
// or an error in case of failures.

package main

import (
	"log"
	"os"

	"golang.org/x/net/context"
	"google.golang.org/grpc"

	pb "shuffler"
)

const (
	address           = "localhost:50051"
	defaultCipherText = "BC234556HJ"
)

// LocalEncoderTestClient is a sample grpc client that talks to the shuffler on
// a local port and verifies the conenction.
func main() {
	conn, err := grpc.Dial(address, grpc.WithInsecure())

	if err != nil {
		log.Fatalf("Unable to connect to server: %v", err)
	}

	defer conn.Close()
	c := pb.NewShufflerClient(conn)

	ciphertext := []byte("test cipher text")
	if len(os.Args) > 1 {
		ciphertext = []byte(os.Args[1])
	}
	_, err = c.Process(context.Background(), &pb.Envelope{
		Manifest: &pb.Manifest{
			ShufflerPolicy: pb.Manifest_UNKNOWN_POLICY,
			Recipient:      &pb.Recipient{HostName: "analyzer_host"}},
		EncryptedMessage: &pb.EncryptedMessage{
			Scheme:     pb.EncryptedMessage_ENVELOPE_1,
			PubKey:     "pub_key",
			Ciphertext: ciphertext}})
	if err != nil {
		log.Fatalf("Could not send encoded reports: %v", err)
	}

	log.Printf("Shuffler grpc call executed successfully")
}
