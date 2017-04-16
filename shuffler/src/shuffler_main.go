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
	"config"
	"dispatcher"
	"flag"
	"path/filepath"
	"receiver"
	"shuffler"
	"storage"
	"time"

	"github.com/golang/glog"
)

var (
	// If true, tls is enabled for both server and client connections
	tls = flag.Bool("tls", false, "Connection uses TLS if true, else plain TCP")

	// shuffler server configuration flags
	certFile = flag.String("cert_file", "", "The TLS cert file")
	keyFile  = flag.String("key_file", "", "The TLS key file")
	port     = flag.Int("port", 50051, "The server port")

	// shuffler client configuration flags to connect to analyzer
	caFile      = flag.String("ca_file", "", "The file containing the CA root certificate")
	timeout     = flag.Int("timeout", 30, "Grpc connection timeout in seconds")
	analyzerURL = flag.String("analyzer_uri", "", "The URL for analyzer service")

	// shuffler dispatch configuration flags
	configFile = flag.String("config_file", "", "The Shuffler config file")
	batchSize  = flag.Int("batch_size", 1000, "The size of ObservationBatch to be sent to Analyzer")

	// shuffler db configuration flags
	useMemStore = flag.Bool("use_memstore", false, "Shuffler uses in memory store if true, else persistent store")
	dbDir       = flag.String("db_dir", "", "Path to the Shuffler local datastore")
)

func main() {
	flag.Parse()

	// Initialize Shuffler configuration
	var sConfig *shuffler.ShufflerConfig
	var err error
	if *configFile == "" {
		glog.Warning("Using Shuffler default configuration. Pass -config_file to specify custom config options.")
		// Use the default config
		sConfig = &shuffler.ShufflerConfig{}
		sConfig.GlobalConfig = &shuffler.Policy{
			FrequencyInHours: 24,
			PObservationDrop: 0.0,
			Threshold:        500,
			DisposalAgeDays:  4,
		}
	} else {
		if sConfig, err = config.LoadConfig(*configFile); err != nil {
			glog.Fatal("Error loading shuffler config file: [", *configFile, "]: ", err)
		}
	}

	// Initialize Shuffler data store
	var store storage.Store
	if *useMemStore {
		glog.Warning("Using MemStore--data will not be persistent. All data will be lost when the Shufler restarts!")
		store = storage.NewMemStore()
	} else {
		if *dbDir == "" {
			glog.Fatal("Either -use_memstore or -db_dir are required.")
		}
		observationsDBpath, err := filepath.Abs(filepath.Join(*dbDir, "observations_db"))
		if err != nil {
			glog.Fatal("%v", err)
		}
		glog.Infof("Using LevelDB store located at %s.", observationsDBpath)
		if store, err = storage.NewLevelDBStore(observationsDBpath); err != nil || store == nil {
			glog.Fatal("Error initializing shuffler datastore: [", *dbDir, "]: ", err)
		}
	}

	// Override analyzer client's url if |analyzerURL| flag is set
	url := sConfig.GetGlobalConfig().AnalyzerUrl
	if *analyzerURL != "" {
		url = *analyzerURL
	}

	grpcAnalyzerClient := dispatcher.NewGrpcAnalyzerTransport(&dispatcher.GrpcClientConfig{
		EnableTLS: *tls,
		CAFile:    *caFile,
		Timeout:   time.Duration(*timeout) * time.Second,
		URL:       url,
	})

	// Start dispatcher and keep polling for dispatch events
	go dispatcher.Start(sConfig, store, *batchSize, grpcAnalyzerClient)

	// Start listening on receiver for incoming requests from Encoder
	receiver.Run(store, &receiver.ServerConfig{
		EnableTLS: *tls,
		CertFile:  *certFile,
		KeyFile:   *keyFile,
		Port:      *port,
	})
}
