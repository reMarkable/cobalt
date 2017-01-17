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

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <string>
#include <map>

#include "analyzer/analyzer_service.h"
#include "analyzer/store/mem_store.h"
#include "analyzer/store/bigtable_store_old.h"

#include "third_party/googletest/googletest/include/gtest/gtest.h"

using google::protobuf::Empty;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

namespace cobalt {
namespace analyzer {

// Fixture to start and stop the bigtable emulator.
class BigtableFunctionalTest : public ::testing::Test {
 public:
  BigtableFunctionalTest() : pid_(0) {}

 protected:
  typedef std::map<std::string, std::string> Map;

  virtual void SetUp() {
    // stdout pipe.
    ASSERT_NE(pipe(pipe_), -1);

    // Start the bigtabe emulator.
    pid_ = fork();
    ASSERT_NE(pid_, -1);

    // Child.
    if (pid_ == 0) {
      ASSERT_NE(dup2(pipe_[1], 1), -1);
      ASSERT_NE(dup2(pipe_[1], 2), -1);
      ASSERT_NE(setpgid(0, 0), -1);

      int rc = execl("/bin/sh", "sh", "-c",
                     "gcloud beta emulators bigtable start", NULL);
      ASSERT_NE(rc, -1);
      exit(0);
    }

    // Parent.
    close(pipe_[1]);

    // Wait for bigtable to start.  Figure out what port it's running on.
    FILE* out = fdopen(pipe_[0], "r");
    ASSERT_NE(out, nullptr);

    char buf[1024];
    char* host = nullptr;
    int port;

    while (fgets(buf, sizeof(buf), out)) {
      host = strstr(buf, "running on ");

      if (host) {
        host += 11;

        char* p = strchr(host, ':');
        ASSERT_NE(p, nullptr);

        *p++ = 0;
        port = atoi(p);
        break;
      }
    }

    ASSERT_NE(host, nullptr);

    // Setup the bigtable client
    snprintf(buf, sizeof(buf), "%s:%d", host, port);
    ASSERT_NE(setenv("BIGTABLE_EMULATOR_HOST", buf, 1), -1);

    BigtableStoreOld* store =
        new BigtableStoreOld("projects/p/instances/i/tables/t");
    store_.reset(store);
    store->initialize(true);
  }

  virtual void TearDown() {
    if (pid_ != 0) {
      kill(-pid_, SIGKILL);
      waitpid(pid_, NULL, 0);
      close(pipe_[0]);
      ASSERT_NE(unsetenv("BIGTABLE_EMULATOR_HOST"), -1);
    }
  }

  // Grabs data from the store from start to end, and expects the result to be
  // equal to the contents of "data".
  void check_range(const std::string& start, const std::string& end,
                   const Map& data) {
    Map result;
    ASSERT_EQ(store_->get_range(start, end, &result), 0);
    ASSERT_EQ(result, data);
  }

  // Returns data[start:end].
  Map slice(const Map& data, int start, int end) {
    Map result;

    auto iter = data.begin();

    for (int i = 0; i <= end; i++) {
      if (iter == data.end()) {
        // XXX - want ASSERT_TRUE(false);
        return result;
      }

      if (i >= start)
        result[iter->first] = iter->second;

      ++iter;
    }

    return result;
  }

  std::unique_ptr<Store> store_;

 private:
  int pid_;
  int pipe_[2];
};

// Put data and try to get different ranges of the data.
TEST_F(BigtableFunctionalTest, TestGetRange) {
  Map data;

  // Generate data and store it.
  for (int i = 0; i < 10; i++) {
    char key[12], val[12];

    snprintf(key, sizeof(key), "k_%d", i);
    snprintf(val, sizeof(val), "v_%d", i);

    data[key] = val;
    ASSERT_EQ(store_->put(key, val), 0);
  }

  check_range("", "", slice(data, 0, 9));
  check_range("k_1", "", slice(data, 1, 9));
  check_range("k_1", "k_8", slice(data, 1, 8));
  check_range("", "k_8", slice(data, 0, 8));
  check_range("k_4", "k_4", slice(data, 4, 4));
}

}  // namespace analyzer
}  // namespace cobalt

int main(int argc, char **argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
