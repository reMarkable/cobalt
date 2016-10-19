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
shuffler/service/shuffler.proto.

A shuffler listens to incoming requests from Encoders (end users),
strips the user metadata like IP-address, timestamps etc before buffering
them locally based on the manifest information provided in the request.
*/

package main

import (
	"flag"

	"github.com/golang/glog"

	"receiver"
)

var (
	tls      = flag.Bool("tls", false, "Connection uses TLS if true, else plain TCP")
	certFile = flag.String("cert_file", "", "The TLS cert file")
	keyFile  = flag.String("key_file", "", "The TLS key file")
	port     = flag.Int("port", 50051, "The server port")
)

func main() {
	flag.Parse()
	if glog.V(2) {
		glog.Info("Listening for incoming encoder requests on port [", *port, "]...")
	}
	receiver.ReceiveAndStore(*tls, *certFile, *keyFile, *port)
}
