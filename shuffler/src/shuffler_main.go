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

package main

import (
	"flag"

	"github.com/golang/glog"

	"config"
	"dispatcher"
	"receiver"
	"storage"
)

var (
	tls        = flag.Bool("tls", false, "Connection uses TLS if true, else plain TCP")
	certFile   = flag.String("cert_file", "", "The TLS cert file")
	keyFile    = flag.String("key_file", "", "The TLS key file")
	port       = flag.Int("port", 50051, "The server port")
	configFile = flag.String("config_file", "", "The Shuffler config file")
	batchSize  = flag.Int("batch_size", 100, "The size of ObservationBatch to be sent to Analyzer")
)

func main() {
	flag.Parse()

	// Initialize Shuffler configuration
	if *configFile == "" {
		glog.Warning("Using Shuffler default configuration...")
		// Use the default config
		*configFile = "./out/shuffler/conf/config_v0.txt"
	}

	config, err := config.LoadConfig(*configFile)
	if err != nil {
		glog.Fatal("Error loading shuffler config file [", *configFile, "]: ", err)
	}

	// Initialize Shuffler data store
	store := storage.NewMemStore()

	// Start dispatcher and keep polling for dispatch events
	go dispatcher.Dispatch(config, store, *batchSize, &dispatcher.GrpcAnalyzer{
		URL: config.GetGlobalConfig().AnalyzerUrl})

	// Start listening on receiver for incoming requests from Encoder
	if glog.V(2) {
		glog.Info("Listening for incoming encoder requests on port [", *port, "]...")
	}
	receiver.ReceiveAndStore(*tls, *certFile, *keyFile, *port, config, store)
}
