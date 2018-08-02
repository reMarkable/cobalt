// Copyright 2017 The Fuchsia Authors
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

#include <iostream>

#include <gflags/gflags.h>
#include "glog/logging.h"
#include "util/crypto_util/cipher.h"

int main(int argc, char* argv[]) {
  google::SetUsageMessage(
      "Generates a new public/private key pair using Cobalt's hybrid "
      "encryption scheme. Copy the keys into files named 'analyzer_public.pem' "
      "and 'analyzer_private.pem' or into files named 'shuffler_public.pem' "
      "and 'shuffler_private.pem' in your root Cobalt src directory");
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  std::string public_key_pem;
  std::string private_key_pem;
  cobalt::crypto::HybridCipher::GenerateKeyPairPEM(&public_key_pem,
                                                   &private_key_pem);
  std::cout << "\n";
  std::cout
      << "Copy the following public key into a file named "
         "'analyzer_public.pem' (or 'shuffler_public.pem')\n"
         "in your Cobalt source root directory.\n\n";
  // The bytes before and after the public_key_pem have the effect of
  // displaying the key in green on the console.
  std::cout << "\x1b[32;1m" << public_key_pem << "\x1b[0m";
  std::cout << "\n";
  // The bytes before and after the private_key_pem have the effect of
  // displaying the key in green on the console.
  std::cout << "Copy the following private key into a file named "
         "'analyzer_private.pem' (or 'shuffler_private.pem')\n"
         "in your Cobalt source root directory.\n\n";
  std::cout << "\x1b[32;1m" << private_key_pem << "\x1b[0m";

  exit(0);
}
