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
// shuffler/shuffler.proto. This test could be used as an end to end
// test for testing functionality between encoders and shufflers. A shuffler
// processes the incoming request and returns an empty response, if successful
// or an error in case of failures.

package main

import (
	"flag"
	"strconv"
	"strings"
	"time"

	"github.com/golang/glog"
	"github.com/golang/protobuf/proto"
	"golang.org/x/net/context"
	"google.golang.org/grpc"

	cobaltpb "cobalt"
	util "util"
)

var (
	tls      = flag.Bool("tls", false, "Connection uses TLS if true, else plain TCP")
	certFile = flag.String("cert_file", "", "The TLS cert file")
	keyFile  = flag.String("key_file", "", "The TLS key file")
	// Shuffler server config
	sIP   = flag.String("s_ip", "localhost", "Shuffler's IP address")
	sPort = flag.Int("s_port", 50051, "Shuffler's server port")
	// Analyzer server config
	aIP      = flag.String("a_ip", "localhost", "Analyzer's IP address")
	aPort    = flag.Int("a_port", 8080, "Analyzer's server port")
	analyzer = flag.Bool("analyzer", false, "Connects to Analyzer directly, if true")

	//help = flag.String("help", "", "Shuffler cmdline options")
)

// Connect to Shuffler
func grpcToShuffler() {
	s := []string{*sIP, ":", strconv.Itoa(*sPort)}
	shufflerAddress := strings.Join(s, "")

	glog.V(2).Info("Connecting to shuffler: ", shufflerAddress)
	conn, err := grpc.Dial(shufflerAddress, grpc.WithInsecure(), grpc.WithTimeout(time.Minute*2))

	if err != nil {
		glog.Fatalf("Unable to connect to Shuffler: %v", err)
	}

	defer conn.Close()
	sc := cobaltpb.NewShufflerClient(conn)

	glog.V(2).Info("Processing a sample envelope...")
	ciphertext := []byte("test cipher text")
	env := &cobaltpb.Envelope{
		Manifest: &cobaltpb.Manifest{
			ShufflerPolicy: cobaltpb.Manifest_UNKNOWN_POLICY,
			RecipientUrl:   strings.Join([]string{*aIP, ":", strconv.Itoa(int(*aPort))}, ""),
			ObservationMetaData: &cobaltpb.ObservationMetadata{
				CustomerId: uint32(123),
				ProjectId:  uint32(456),
				MetricId:   uint32(678),
				DayIndex:   uint32(7),
			},
		},
		EncryptedMessage: &cobaltpb.EncryptedMessage{
			Scheme:     cobaltpb.EncryptedMessage_PK_SCHEME_1,
			PubKey:     "pub_key",
			Ciphertext: ciphertext}}

	data, err := proto.Marshal(env)
	if err != nil {
		glog.Fatalf("Error in marshalling envelope data: %v", err)
	}
	testPubKey := "pub_key"
	c := util.NoOpCrypter{}
	sendMsg := &cobaltpb.EncryptedMessage{
		PubKey:     testPubKey,
		Ciphertext: c.Encrypt(data, testPubKey)}

	_, err = sc.Process(context.Background(), sendMsg)
	if err != nil {
		glog.Fatalf("Could not send encoded reports: %v", err)
	}

	glog.Info("Success: Grpc to Shuffler service was successfully completed.")
}

// Conenct to Analyzer directly
func grpcToAnalyzer() {
	s := []string{*aIP, ":", strconv.Itoa(int(*aPort))}
	analyzerAddress := strings.Join(s, "")

	glog.Info("Connecting to analyzer: %v", analyzerAddress)

	conn, err := grpc.Dial(analyzerAddress, grpc.WithInsecure(), grpc.WithTimeout(time.Second*30))

	if err != nil {
		glog.Fatalf("Unable to connect to Analyzer: %v", err)
	}

	defer conn.Close()
	c := cobaltpb.NewAnalyzerClient(conn)

	ciphertext := []byte("test cipher text")
	_, err = c.AddObservations(context.Background(), &cobaltpb.ObservationBatch{
		EncryptedObservation: []*cobaltpb.EncryptedMessage{
			&cobaltpb.EncryptedMessage{
				Scheme:     cobaltpb.EncryptedMessage_PK_SCHEME_1,
				PubKey:     "pub_key",
				Ciphertext: ciphertext}}})

	if err != nil {
		glog.Fatalf("Could not send reports to analyzer: %v", err)
	}

	glog.Info("Success: Grpc to Analyzer service was successfully completed.")
}

func main() {
	flag.Parse()
	if *analyzer {
		grpcToAnalyzer()
	} else {
		grpcToShuffler()
	}
	glog.Flush()
}
