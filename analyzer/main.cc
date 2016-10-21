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

#include <err.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "analyzer/analyzer.h"
#include "analyzer/store/bigtable_store.h"
#include "analyzer/store/mem_store.h"

int main(int argc, char *argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  if (argc < 2) {
    errx(1, "Usage: %s <table_name>\n"
            "E.g., projects/google.com:shuffler-test/instances/cobalt-analyzer/"
            "tables/observations\n"
            "Use the special \"mem\" tablename for an in-memory datastore"
            , argv[0]);
  }

  LOG(INFO) << "Starting analyzer";

  cobalt::analyzer::BigtableStore bigtable(argv[1]);
  cobalt::analyzer::MemStore mem;
  cobalt::analyzer::Store* store;

  if (strcmp(argv[1], "mem") == 0) {
    LOG(INFO) << "Using a memory store";
    store = &mem;
  } else {
    if (bigtable.initialize() < 0)
      LOG(FATAL) << "Cannot initialzie Bigtable";

    store = &bigtable;
  }

  cobalt::analyzer::AnalyzerServiceImpl analyzer(store);
  analyzer.Start();
  analyzer.Wait();
}
