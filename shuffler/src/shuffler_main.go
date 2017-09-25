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
	"io/ioutil"
	"path/filepath"
	"receiver"
	"time"

	"dispatcher"
	"shuffler"
	"shuffler_config"
	"storage"

	"github.com/golang/glog"
)

var (
	// If true, tls is enabled for both server and client connections
	tls             = flag.Bool("tls", false, "Connection uses TLS if true, else plain TCP")
	tls_to_analyzer = flag.Bool("tls_to_analyzer", false, "Use TLS to connect to the analyzer")

	// shuffler server configuration flags
	certFile = flag.String("cert_file", "", "The TLS cert file")
	keyFile  = flag.String("key_file", "", "The TLS key file")
	port     = flag.Int("port", 50051, "The server port")

	privateKeyPemFile = flag.String("private_key_pem_file", "",
		"Path to a file containing a PEM encoding of the private key of "+
			"the Shuffler used for Cobalt's internal encryption scheme. If "+
			"not specified then the Shuffler will not support encrypted Envelopes.")

	// shuffler client configuration flags to connect to analyzer
	caFile      = flag.String("ca_file", "", "The file containing the CA root certificate")
	timeout     = flag.Int("timeout", 30, "Grpc connection timeout in seconds")
	analyzerURL = flag.String("analyzer_uri", "", "The URL for analyzer service")

	// shuffler dispatch configuration flags
	configFile = flag.String("config_file", "", "The Shuffler config file")
	batchSize  = flag.Int("batch_size", 1000, "The size of ObservationBatch to be sent to Analyzer")

	// shuffler db configuration flags
	useMemStore   = flag.Bool("use_memstore", false, "Shuffler uses in memory store if true, else persistent store")
	dbDir         = flag.String("db_dir", "", "Path to the Shuffler local datastore")
	deleteAllData = flag.Bool("danger_danger_delete_all_data_at_startup", false,
		"If true then upon startup all data from previous executions of the Shuffler will be deleted. "+
			"This should not be set true in normal shuffler operation.")
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
		if sConfig, err = shuffler_config.LoadConfig(*configFile); err != nil {
			glog.Fatal("Error loading shuffler config file: [", *configFile, "]: ", err)
		}
	}

	// Read the private key PEM file
	privateKeyPem := ""
	if *privateKeyPemFile != "" {
		if fileContents, err := ioutil.ReadFile(*privateKeyPemFile); err != nil {
			glog.Errorf("Error attempting to read private key PEM file %s: %v. "+
				"The shuffler will not be able to decrypt EncryptedMessages.", *privateKeyPemFile, err)
		} else {
			glog.Infof("Successfully read private key PEM file %s.", *privateKeyPemFile)
			privateKeyPem = string(fileContents)
		}
	} else {
		glog.Warning("The flag -private_key_pem_file was not provided. The shuffler will not be able to decrypt EncryptedMessages.")
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
		if *deleteAllData {
			glog.Warning("*** WARNING: DELETING ALL DATA FROM SHUFFLER'S DATA STORE!!! ***")
			glog.Warning("The flag -danger_danger_delete_all_data_at_startup was passed.")
			store.(*storage.LevelDBStore).EraseAllData()
		}
	}

	// Override analyzer client's url if |analyzerURL| flag is set
	url := sConfig.GetGlobalConfig().AnalyzerUrl
	if *analyzerURL != "" {
		url = *analyzerURL
	}

	grpcAnalyzerClient := dispatcher.NewGrpcAnalyzerTransport(&dispatcher.GrpcClientConfig{
		EnableTLS: *tls_to_analyzer,
		CAFile:    *caFile,
		Timeout:   time.Duration(*timeout) * time.Second,
		URL:       url,
	})

	// Start dispatcher and keep polling for dispatch events
	go dispatcher.Start(sConfig, store, *batchSize, grpcAnalyzerClient)

	// Start listening on receiver for incoming requests from Encoder
	receiver.Run(store, &receiver.ServerConfig{
		EnableTLS:     *tls,
		CertFile:      *certFile,
		KeyFile:       *keyFile,
		Port:          *port,
		PrivateKeyPem: privateKeyPem,
	})
}
