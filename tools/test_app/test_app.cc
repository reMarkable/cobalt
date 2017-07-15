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

#include "tools/test_app/test_app.h"

#include <libgen.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "./observation.pb.h"
#include "analyzer/analyzer_service/analyzer.grpc.pb.h"
#include "config/encoding_config.h"
#include "encoder/encoder.h"
#include "encoder/envelope_maker.h"
#include "encoder/project_context.h"
#include "encoder/shuffler_client.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "grpc++/grpc++.h"
#include "util/pem_util.h"

namespace cobalt {

using analyzer::Analyzer;
using config::EncodingRegistry;
using config::MetricRegistry;
using encoder::ClientSecret;
using encoder::Encoder;
using encoder::EnvelopeMaker;
using encoder::ProjectContext;
using encoder::ShufflerClient;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using google::protobuf::Empty;
using shuffler::Shuffler;
using util::PemUtil;

// There are three modes of operation of the Cobalt TestClient program
// determined by the value of this flag.
// - interactive: The program runs an interactive command-loop.
// - send-once: The program sends a single Envelope described by flags.
// - automatic: The program runs forever sending many Envelopes with randomly
//              generated values.
//
// This flag is read in CreateFromFlagsOrDie() and used to invoke
// set_mode().
DEFINE_string(mode, "interactive",
              "This program may be used in 3 modes: 'interactive', "
              "'send-once', 'automatic'");

// The remainder of the flags fall into three categories.

// Category 1: Flags read by CreateFromFlagsOrDie() that set immutable
// values used in all three modes.
DEFINE_uint32(customer, 1, "Customer ID");
DEFINE_uint32(project, 1, "Project ID");
DEFINE_string(analyzer_uri, "",
              "The URI of the Analyzer. Necessary only if sending observations "
              "to the Analyzer.");
DEFINE_string(shuffler_uri, "",
              "The URI of the Shuffler. Necessary only if sending observations "
              "to the Shuffler.");
DEFINE_string(analyzer_pk_pem_file, "",
              "Path to a file containing a PEM encoding of the public key of "
              "the Analyzer used for Cobalt's internal encryption scheme. If "
              "not specified then no encryption will be used.");
DEFINE_string(shuffler_pk_pem_file, "",
              "Path to a file containing a PEM encoding of the public key of "
              "the Shuffler used for Cobalt's internal encryption scheme. If "
              "not specified then no encryption will be used.");
DEFINE_bool(
    use_tls, false,
    "Should tls be used for the connection to the shuffler or the analyzer?");
DEFINE_uint32(deadline_seconds, 10, "RPC deadline.");
DEFINE_string(registry, "", "Directory path of config registry.  Optional.");

// Category 2: Flags consumed by CreateFromFlagsOrDie() that set values that
// may be overidden by a set command in interactive mode.
DEFINE_uint32(metric, 1, "Metric ID to use.");
DEFINE_bool(skip_shuffler, false,
            "If true send Observations directly to the analyzer.");

// Category 3: Flags used only in send-once or automatic modes. These are not
// consumed by CreateFromFlagsOrDie().
DEFINE_uint32(num_clients, 1,
              "Number of clients to simulate in the non-interactive modes.");
DEFINE_string(
    values, "",
    "A comma-separated list of colon-delimited triples of the form"
    " <part>:<val>:<encoding> where <part> is the name of a metric part and"
    " <val> is a string or int value and <encoding> is an EncodingConfig id."
    " Used only in send-once mode to specify the multi-part value to"
    " encode and the encodings to use.");

namespace {

// Prints help for the interactive mode.
void PrintHelp(std::ostream* ostream) {
  *ostream << std::endl;
  *ostream << "Cobalt command-line testing client" << std::endl;
  *ostream << "----------------------------------" << std::endl;
  *ostream << "help                     \tPrint this help message."
           << std::endl;
  *ostream << "encode <num> <val>       \tEncode <num> independent copies "
              "of the string or integer value <val>."
           << std::endl;
  *ostream << std::endl;
  *ostream << "encode <num> <part>:<val>:<encoding> "
              "<part>:<val>:<encoding>..."
           << std::endl;
  *ostream << "                         \tEncode <num> independent copies of "
              "a multi-part value. Each <part> is a part name."
           << std::endl;
  *ostream << "                         \tEach <val> is an int or string "
              "value. Each <encoding> is an EncodingConfig id."
           << std::endl;
  *ostream << std::endl;
  *ostream << "ls                       \tList current values of "
              "parameters."
           << std::endl;
  *ostream << "send                     \tSend all previously encoded "
              "observations and clear the observation cache."
           << std::endl;
  *ostream << "set encoding <id>        \tSet encoding config id." << std::endl;
  *ostream << "set metric <id>          \tSet metric id." << std::endl;
  *ostream << "set skip_shuffler <bool> \tSet skip_shuffler." << std::endl;
  *ostream << "show config              \tDisplay the current Metric and "
              "Encoding configurations."
           << std::endl;
  *ostream << "quit                     \tQuit." << std::endl;
  *ostream << std::endl;
}

// Returns the path to the standard Cobalt demo configuration directory
// based on the presumed location of this binary.
std::string FindConfigRegistrationDirectory(char* argv[]) {
  char path[PATH_MAX], path2[PATH_MAX];

  // Get the directory of this binary.
  if (!realpath(argv[0], path)) {
    LOG(FATAL) << "realpath(): " << argv[0];
  }
  char* dir = dirname(path);

  // Set the relative path to the registry.
  snprintf(path2, sizeof(path2), "%s/../../../config/demo", dir);

  // Get the absolute path to the registry.
  if (!realpath(path2, path)) {
    LOG(FATAL) << "Computed path to config directory is invalid: " << path;
  }

  return path;
}

// Parses the mode flag.
TestApp::Mode ParseMode() {
  if (FLAGS_mode == "interactive") {
    return TestApp::kInteractive;
  }
  if (FLAGS_mode == "send-once") {
    return TestApp::kSendOnce;
  }
  if (FLAGS_mode == "automatic") {
    return TestApp::kAutomatic;
  }
  LOG(FATAL) << "Unrecognized mode: " << FLAGS_mode;
}

// Reads the PEM file at the specified path and writes the contents into
// |*pem_out|. Returns true for success or false for failure.
bool ReadPublicKeyPem(const std::string& pem_file, std::string* pem_out) {
  VLOG(1) << "Reading PEM file at " << pem_file;
  if (PemUtil::ReadTextFile(pem_file, pem_out)) {
    return true;
  }
  LOG(ERROR) << "Unable to open PEM file at " << pem_file
             << ". Skipping encryption!";
  return false;
}

// Reads the config files from the given directory. Returns a ProjectContext
// containing the read config and the values of the -customer and
// -project flags.
std::shared_ptr<ProjectContext> LoadProjectContext(
    const std::string& registration_dir_path) {
  VLOG(1) << "Loading Cobalt configuration from " << registration_dir_path;

  // Load the encoding registry.
  char fname[PATH_MAX];
  snprintf(fname, sizeof(fname), "%s/registered_encodings.txt",
           registration_dir_path.c_str());
  auto encodings = EncodingRegistry::FromFile(fname, nullptr);
  if (encodings.second != config::kOK) {
    LOG(FATAL) << "Can't load encodings configuration";
  }
  std::shared_ptr<EncodingRegistry> encoding_registry(
      encodings.first.release());

  // Load the metrics registry.
  snprintf(fname, sizeof(fname), "%s/registered_metrics.txt",
           registration_dir_path.c_str());
  auto metrics = MetricRegistry::FromFile(fname, nullptr);
  if (metrics.second != config::kOK) {
    LOG(FATAL) << "Can't load metrics configuration";
  }
  std::shared_ptr<MetricRegistry> metric_registry(metrics.first.release());

  CHECK(FLAGS_project < 100) << "-project=" << FLAGS_project
                             << " not allowed. Project ID must be less than "
                                "100 because this tool is not "
                                "intended to mutate real customer projects.";

  return std::shared_ptr<ProjectContext>(new ProjectContext(
      FLAGS_customer, FLAGS_project, metric_registry, encoding_registry));
}

bool ParseBool(const std::string& str) {
  return (str == "true" || str == "True" || str == "1");
}

// Given a |line| of text, breaks it into tokens separated by white space.
std::vector<std::string> Tokenize(const std::string& line) {
  std::istringstream line_stream(line);
  std::vector<std::string> tokens;
  do {
    std::string token;
    line_stream >> token;
    std::remove(token.begin(), token.end(), ' ');
    if (!token.empty()) {
      tokens.push_back(token);
    }
  } while (line_stream);
  return tokens;
}

// Given a |line| of text, breaks it into cells separated by commas.
std::vector<std::string> ParseCSV(const std::string& line) {
  std::stringstream line_stream(line);
  std::vector<std::string> cells;

  std::string cell;
  while (std::getline(line_stream, cell, ',')) {
    std::remove(cell.begin(), cell.end(), ' ');
    if (!cell.empty()) {
      cells.push_back(cell);
    }
  }
  return cells;
}

std::shared_ptr<grpc::ChannelCredentials> CreateChannelCredentials(
    bool use_tls, const char* pem_root_certs = nullptr) {
  if (use_tls) {
    auto opts = grpc::SslCredentialsOptions();
    if (pem_root_certs) {
      opts.pem_root_certs = pem_root_certs;
    }
    return grpc::SslCredentials(opts);
  } else {
    return grpc::InsecureChannelCredentials();
  }
}

}  // namespace

// An implementation of EnvelopeSenderInterface that actually sends Envelopes.
class EnvelopeSender : public EnvelopeSenderInterface {
 public:
  // The mode is used only to determine whether to print error messages to
  // the logs or the the console.
  EnvelopeSender(std::unique_ptr<analyzer::Analyzer::Stub> analyzer_client,
                 std::unique_ptr<encoder::ShufflerClient> shuffler_client,
                 TestApp::Mode mode)
      : analyzer_client_(std::move(analyzer_client)),
        shuffler_client_(std::move(shuffler_client)),
        mode_(mode) {}

  void Send(const EnvelopeMaker& envelope_maker, bool skip_shuffler) override {
    if (skip_shuffler) {
      SendToAnalyzer(envelope_maker);
    } else {
      SendToShuffler(envelope_maker);
    }
  }

 private:
  void SendToAnalyzer(const EnvelopeMaker& envelope_maker) {
    if (!analyzer_client_) {
      if (mode_ == TestApp::kInteractive) {
        std::cout
            << "The flag -analyzer_uri was not specified so you cannot "
               "send directly to the analyzer. Try 'set skip_shuffler false'."
            << std::endl;
      } else {
        LOG(ERROR) << "-analyzer_uri was not specified.";
      }
      return;
    }

    if (envelope_maker.envelope().batch_size() == 0) {
      if (mode_ == TestApp::kInteractive) {
        std::cout << "There are no Observations to send yet." << std::endl;
      } else {
        LOG(ERROR) << "Not sending to analyzer. No observations were "
                      "successfully encoded.";
      }
      return;
    }

    Empty resp;

    for (const ObservationBatch& batch : envelope_maker.envelope().batch()) {
      if (mode_ == TestApp::kInteractive) {
      } else {
        VLOG(1) << "Sending to analyzer with deadline = "
                << FLAGS_deadline_seconds << " seconds...";
      }
      std::unique_ptr<grpc::ClientContext> context(new grpc::ClientContext());
      context->set_deadline(std::chrono::system_clock::now() +
                            std::chrono::seconds(FLAGS_deadline_seconds));

      auto status =
          analyzer_client_->AddObservations(context.get(), batch, &resp);
      if (status.ok()) {
        if (mode_ == TestApp::kInteractive) {
          std::cout << "Sent to Analyzer" << std::endl;
        } else {
          VLOG(1) << "Sent to Analyzer";
        }
      } else {
        if (mode_ == TestApp::kInteractive) {
          std::cout << "Send to analyzer failed with status="
                    << status.error_code() << " " << status.error_message()
                    << std::endl;
        } else {
          LOG(ERROR) << "Send to analyzer failed with status="
                     << status.error_code() << " " << status.error_message();
        }
        return;
      }
    }
  }

  void SendToShuffler(const EnvelopeMaker& envelope_maker) {
    if (!shuffler_client_) {
      if (mode_ == TestApp::kInteractive) {
        std::cout << "The flag -shuffler_uri was not specified so you cannot "
                     "send to the shuffler. Try 'set skip_shuffler true'."
                  << std::endl;
      } else {
        LOG(ERROR) << "-shuffler_uri was not specified.";
      }
      return;
    }

    if (envelope_maker.envelope().batch_size() == 0) {
      if (mode_ == TestApp::kInteractive) {
        std::cout << "There are no Observations to send yet." << std::endl;
      } else {
        LOG(ERROR) << "Not sending to Shuffler. No observations were "
                      "successfully encoded.";
      }
      return;
    }

    if (mode_ == TestApp::kInteractive) {
    } else {
      VLOG(1) << "Sending to shuffler with deadline = "
              << FLAGS_deadline_seconds << " seconds...";
    }

    // Encrypt the envelope.
    EncryptedMessage encrypted_envelope;
    if (!envelope_maker.MakeEncryptedEnvelope(&encrypted_envelope)) {
      LOG(ERROR) << "Encryption of Envelope failed.";
      return;
    }

    for (int attempt = 0; attempt < 3; attempt++) {
      std::unique_ptr<grpc::ClientContext> context(new grpc::ClientContext());
      context->set_deadline(std::chrono::system_clock::now() +
                            std::chrono::seconds(FLAGS_deadline_seconds));
      auto status =
          shuffler_client_->SendToShuffler(encrypted_envelope, context.get());
      if (status.ok()) {
        if (mode_ == TestApp::kInteractive) {
          std::cout << "Sent to Shuffler." << std::endl;
        } else {
          VLOG(1) << "Sent to Shuffler";
        }
        return;
      } else {
        if (mode_ == TestApp::kInteractive) {
          std::cout << "Send to shuffler failed with status="
                    << status.error_code() << " " << status.error_message()
                    << std::endl;
          return;
        } else {
          LOG(ERROR) << "Send to shuffler failed with status="
                     << status.error_code() << " " << status.error_message()
                     << ". Will try three times.";
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
      }
    }
  }

  std::unique_ptr<analyzer::Analyzer::Stub> analyzer_client_;
  std::unique_ptr<encoder::ShufflerClient> shuffler_client_;
  TestApp::Mode mode_;
};

std::unique_ptr<TestApp> TestApp::CreateFromFlagsOrDie(int argc, char* argv[]) {
  std::string registry_path = FLAGS_registry;
  // If no path is given, try to deduce it from the binary location.
  if (registry_path == "") {
    registry_path = FindConfigRegistrationDirectory(argv);
  }

  std::shared_ptr<encoder::ProjectContext> project_context =
      LoadProjectContext(registry_path);

  CHECK(!FLAGS_analyzer_uri.empty() || !FLAGS_shuffler_uri.empty())
      << "You must specify either -shuffler_uri or -analyzer_uri";

  std::unique_ptr<analyzer::Analyzer::Stub> analyzer;
  if (!FLAGS_analyzer_uri.empty()) {
    analyzer = Analyzer::NewStub(grpc::CreateChannel(
        FLAGS_analyzer_uri, CreateChannelCredentials(FLAGS_use_tls)));
  }

  std::unique_ptr<encoder::ShufflerClient> shuffler_client;
  if (!FLAGS_shuffler_uri.empty()) {
    VLOG(1) << "Connecting to Shuffler at " << FLAGS_shuffler_uri;
    shuffler_client.reset(
        new ShufflerClient(FLAGS_shuffler_uri, FLAGS_use_tls));
  }

  auto mode = ParseMode();
  std::shared_ptr<EnvelopeSender> envelope_sender(new EnvelopeSender(
      std::move(analyzer), std::move(shuffler_client), mode));

  auto analyzer_encryption_scheme = EncryptedMessage::NONE;
  std::string analyzer_public_key_pem = "";
  if (FLAGS_analyzer_pk_pem_file.empty()) {
    VLOG(1) << "WARNING: Encryption of Observations to the Analzyer not being "
               "used. Pass the flag -analyzer_pk_pem_file";
  } else if (ReadPublicKeyPem(FLAGS_analyzer_pk_pem_file,
                              &analyzer_public_key_pem)) {
    analyzer_encryption_scheme = EncryptedMessage::HYBRID_ECDH_V1;
  }
  auto shuffler_encryption_scheme = EncryptedMessage::NONE;
  std::string shuffler_public_key_pem = "";
  if (FLAGS_shuffler_pk_pem_file.empty()) {
    VLOG(1) << "WARNING: Encryption of Envelopes to the Shuffler not being "
               "used. Pass the flag -shuffler_pk_pem_file";
  } else if (ReadPublicKeyPem(FLAGS_shuffler_pk_pem_file,
                              &shuffler_public_key_pem)) {
    shuffler_encryption_scheme = EncryptedMessage::HYBRID_ECDH_V1;
  }

  auto test_app = std::unique_ptr<TestApp>(
      new TestApp(project_context, envelope_sender, analyzer_public_key_pem,
                  analyzer_encryption_scheme, shuffler_public_key_pem,
                  shuffler_encryption_scheme, &std::cout));
  test_app->set_metric(FLAGS_metric);
  test_app->set_skip_shuffler(FLAGS_skip_shuffler);
  test_app->set_mode(mode);
  return test_app;
}

TestApp::TestApp(std::shared_ptr<ProjectContext> project_context,
                 std::shared_ptr<EnvelopeSenderInterface> sender,
                 const std::string& analyzer_public_key_pem,
                 EncryptedMessage::EncryptionScheme analyzer_scheme,
                 const std::string& shuffler_public_key_pem,
                 EncryptedMessage::EncryptionScheme shuffler_scheme,
                 std::ostream* ostream)
    : customer_id_(project_context->customer_id()),
      project_id_(project_context->project_id()),
      project_context_(project_context),
      envelope_sender_(sender),
      ostream_(ostream) {
  envelope_maker_.reset(
      new EnvelopeMaker(analyzer_public_key_pem, analyzer_scheme,
                        shuffler_public_key_pem, shuffler_scheme));
}

void TestApp::Run() {
  switch (mode_) {
    case kInteractive:
      CommandLoop();
      break;
    case kSendOnce:
      SendAndQuit();
      break;
    case kAutomatic:
      RunAutomatic();
      break;
  }
}

void TestApp::RunAutomatic() {
  // TODO(rudominer) Implement automatic mode.
  LOG(FATAL) << "automatic mode is not yet implemented.";
}

void TestApp::SendAndQuit() {
  auto value_triples = ParseCSV(FLAGS_values);
  if (value_triples.empty()) {
    LOG(ERROR) << "--values was not set.";
    return;
  }

  std::vector<std::string> part_names;
  std::vector<std::string> values;
  std::vector<uint32_t> encoding_config_ids;
  for (const auto& triple : value_triples) {
    part_names.emplace_back();
    values.emplace_back();
    encoding_config_ids.emplace_back();
    if (!ParsePartValueEncodingTriple(triple, &part_names.back(),
                                      &values.back(),
                                      &encoding_config_ids.back())) {
      LOG(ERROR)
          << "Malformed <part>:<value>:<encoding> triple in --values flag: "
          << triple;
      return;
    }
  }

  Encode(encoding_config_ids, part_names, values);

  envelope_sender_->Send(*envelope_maker_, skip_shuffler_);
}

void TestApp::CommandLoop() {
  std::string command_line;
  while (true) {
    *ostream_ << "Command or 'help': ";
    getline(std::cin, command_line);
    if (!ProcessCommandLine(command_line)) {
      break;
    }
  }
}

// Generates FLAGS_num_clients independent Observations by encoding the
// multi-part value specified by the arguments and adds the Observations
// to the EnvelopeMaker.
void TestApp::Encode(const std::vector<uint32_t> encoding_config_ids,
                     const std::vector<std::string>& metric_parts,
                     const std::vector<std::string>& values) {
  for (size_t i = 0; i < FLAGS_num_clients; i++) {
    if (!EncodeAsNewClient(encoding_config_ids, metric_parts, values)) {
      break;
    }
  }
}

// Generates a new ClientSecret, constructs a new Encoder using that secret,
// uses this Encoder to encode the multi-part value specified by the
// arguments, and adds the resulting Observation to the EnvelopeMaker.
bool TestApp::EncodeAsNewClient(const std::vector<uint32_t> encoding_config_ids,
                                const std::vector<std::string>& metric_parts,
                                const std::vector<std::string>& values) {
  size_t num_parts = metric_parts.size();
  CHECK_EQ(num_parts, values.size());
  CHECK_EQ(num_parts, encoding_config_ids.size());

  // Build the |Value|.
  Encoder::Value value;
  for (size_t i = 0; i < num_parts; i++) {
    int64_t int_val;
    if (ParseInt(values[i], false, &int_val)) {
      value.AddIntPart(encoding_config_ids[i], metric_parts[i], int_val);
    } else {
      value.AddStringPart(encoding_config_ids[i], metric_parts[i], values[i]);
    }
  }

  // Construct a new Encoder.
  std::unique_ptr<Encoder> encoder(
      new Encoder(project_context_, ClientSecret::GenerateNewSecret()));

  // Use the Encoder to encode the Value.
  auto result = encoder->Encode(metric_, value);

  if (result.status != Encoder::kOK) {
    LOG(ERROR) << "Encode() failed with status " << result.status
               << ". metric_id=" << metric_ << ". Multi-part value:";
    for (size_t i = 0; i < num_parts; i++) {
      LOG(ERROR) << metric_parts[i] << ":" << values[i]
                 << " encoding=" << encoding_config_ids[i];
    }
    return false;
  }

  // Add the observation to the EnvelopeMaker.
  envelope_maker_->AddObservation(*result.observation,
                                  std::move(result.metadata));
  return true;
}

// Generates FLAGS_num_clients independent Observations by encoding the
// string value specified by the argument and adds the Observations
// to the EnvelopeMaker.
void TestApp::EncodeString(const std::string value) {
  for (size_t i = 0; i < FLAGS_num_clients; i++) {
    if (!EncodeStringAsNewClient(value)) {
      break;
    }
  }
}

// Generates a new ClientSecret, constructs a new Encoder using that secret,
// uses this Encoder to encode the string value specified by the
// argument, and adds the resulting Observation to the EnvelopeMaker.
bool TestApp::EncodeStringAsNewClient(const std::string value) {
  std::unique_ptr<Encoder> encoder(
      new Encoder(project_context_, ClientSecret::GenerateNewSecret()));
  auto result = encoder->EncodeString(metric_, encoding_config_id_, value);
  if (result.status != Encoder::kOK) {
    LOG(ERROR) << "EncodeString() failed with status " << result.status
               << ". metric_id=" << metric_
               << ". encoding_config_id=" << encoding_config_id_
               << ". value=" << value;
    return false;
  }
  envelope_maker_->AddObservation(*result.observation,
                                  std::move(result.metadata));
  return true;
}

// Generates FLAGS_num_clients independent Observations by encoding the
// int value specified by the argument and adds the Observations
// to the EnvelopeMaker.
void TestApp::EncodeInt(int64_t value) {
  for (size_t i = 0; i < FLAGS_num_clients; i++) {
    if (!EncodeIntAsNewClient(value)) {
      break;
    }
  }
}

// Generates a new ClientSecret, constructs a new Encoder using that secret,
// uses this Encoder to encode the int value specified by the
// argument, and adds the resulting Observation to the EnvelopeMaker.
bool TestApp::EncodeIntAsNewClient(int64_t value) {
  std::unique_ptr<Encoder> encoder(
      new Encoder(project_context_, ClientSecret::GenerateNewSecret()));
  auto result = encoder->EncodeInt(metric_, encoding_config_id_, value);
  if (result.status != Encoder::kOK) {
    LOG(ERROR) << "EncodeInt() failed with status " << result.status
               << ". metric_id=" << metric_
               << ". encoding_config_id=" << encoding_config_id_
               << ". value=" << value;
    return false;
  }
  envelope_maker_->AddObservation(*result.observation,
                                  std::move(result.metadata));
  return true;
}

bool TestApp::ProcessCommandLine(const std::string command_line) {
  return ProcessCommand(Tokenize(command_line));
}

bool TestApp::ProcessCommand(const std::vector<std::string>& command) {
  if (command.empty()) {
    return true;
  }

  if (command[0] == "help") {
    PrintHelp(ostream_);
    return true;
  }

  if (command[0] == "encode") {
    Encode(command);
    return true;
  }

  if (command[0] == "ls") {
    ListParameters();
    return true;
  }

  if (command[0] == "send") {
    Send(command);
    return true;
  }

  if (command[0] == "set") {
    SetParameter(command);
    return true;
  }

  if (command[0] == "show") {
    Show(command);
    return true;
  }

  if (command[0] == "quit") {
    return false;
  }

  *ostream_ << "Unrecognized command: " << command[0] << std::endl;

  return true;
}

void TestApp::Encode(const std::vector<std::string>& command) {
  if (command.size() < 3) {
    *ostream_ << "Malformed encode command. Expected 2 additional arguments."
              << std::endl;
    return;
  }

  if (command.size() > 3 || IsTriple(command[2])) {
    EncodeMulti(command);
    return;
  }

  int64_t num_clients;
  if (!ParseInt(command[1], true, &num_clients)) {
    return;
  }
  if (num_clients <= 0) {
    *ostream_ << "<num> must be a positive integer: " << num_clients
              << std::endl;
    return;
  }
  FLAGS_num_clients = num_clients;

  int64_t int_val;
  if (ParseInt(command[2], false, &int_val)) {
    EncodeInt(int_val);
  } else {
    EncodeString(command[2]);
  }
}

void TestApp::EncodeMulti(const std::vector<std::string>& command) {
  CHECK_GE(command.size(), 3u);

  int64_t num_clients;
  if (!ParseInt(command[1], true, &num_clients)) {
    return;
  }
  if (num_clients <= 0) {
    *ostream_ << "<num> must be a positive integer: " << num_clients
              << std::endl;
  }
  FLAGS_num_clients = num_clients;

  std::vector<std::string> part_names;
  std::vector<std::string> values;
  std::vector<uint32_t> encoding_config_ids;
  for (size_t i = 2; i < command.size(); i++) {
    part_names.emplace_back();
    values.emplace_back();
    encoding_config_ids.emplace_back();
    if (!ParsePartValueEncodingTriple(command[i], &part_names.back(),
                                      &values.back(),
                                      &encoding_config_ids.back())) {
      *ostream_
          << "Malformed <part>:<value>:<encoding> triple in encode command: "
          << command[i] << std::endl;
      return;
    }
  }

  Encode(encoding_config_ids, part_names, values);
}

void TestApp::ListParameters() {
  *ostream_ << std::endl;
  *ostream_ << "Settable values" << std::endl;
  *ostream_ << "---------------" << std::endl;
  *ostream_ << "Metric ID: " << metric_ << std::endl;
  *ostream_ << "Encoding Config ID: " << encoding_config_id_ << std::endl;
  *ostream_ << "Skip Shuffler: " << skip_shuffler_ << std::endl;
  *ostream_ << std::endl;
  *ostream_ << "Values set by flag at startup." << std::endl;
  *ostream_ << "-----------------------------" << std::endl;
  *ostream_ << "Customer ID: " << customer_id_ << std::endl;
  *ostream_ << "Project ID: " << project_id_ << std::endl;
  *ostream_ << "Analyzer URI: " << FLAGS_analyzer_uri << std::endl;
  *ostream_ << "Shuffler URI: " << FLAGS_shuffler_uri << std::endl;
  *ostream_ << std::endl;
}

void TestApp::SetParameter(const std::vector<std::string>& command) {
  if (command.size() != 3) {
    *ostream_ << "Malformed set command. Expected 2 additional arguments."
              << std::endl;
    return;
  }

  if (command[1] == "metric") {
    int64_t id;
    if (!ParseInt(command[2], true, &id)) {
      return;
    }
    if (id <= 0) {
      *ostream_ << "<id> must be a positive integer";
      return;
    }
    metric_ = id;
  } else if (command[1] == "encoding") {
    int64_t id;
    if (!ParseInt(command[2], true, &id)) {
      return;
    }
    if (id <= 0) {
      *ostream_ << "<id> must be a positive integer";
      return;
    }
    encoding_config_id_ = id;
  } else if (command[1] == "skip_shuffler") {
    skip_shuffler_ = ParseBool(command[2]);
  } else {
    *ostream_ << command[1] << " is not a settable parameter." << std::endl;
  }
}

void TestApp::Send(const std::vector<std::string>& command) {
  if (command.size() != 1) {
    *ostream_ << "The send command doesn't take any arguments." << std::endl;
    return;
  }
  envelope_sender_->Send(*envelope_maker_, skip_shuffler_);

  envelope_maker_->Clear();
}

void TestApp::Show(const std::vector<std::string>& command) {
  // show config is currently the only show command.
  if (command.size() != 2 || command[1] != "config") {
    *ostream_ << "Expected 'show config'." << std::endl;
    return;
  }

  auto* metric = project_context_->Metric(metric_);
  if (!metric) {
    *ostream_ << "There is no metric with id=" << metric_ << "." << std::endl;
  } else {
    *ostream_ << "Metric " << metric->id() << std::endl;
    *ostream_ << "-----------" << std::endl;
    ShowMetric(*metric);
    *ostream_ << std::endl;
  }

  auto* encoding = project_context_->EncodingConfig(encoding_config_id_);
  if (!encoding) {
    *ostream_ << "There is no encoding config with id=" << encoding_config_id_
              << "." << std::endl;
  } else {
    *ostream_ << "Encoding Config " << encoding->id() << std::endl;
    *ostream_ << "--------------------" << std::endl;
    ShowEncodingConfig(*encoding);
    *ostream_ << std::endl;
  }
}

void TestApp::ShowMetric(const Metric& metric) {
  *ostream_ << metric.name() << std::endl;
  *ostream_ << metric.description() << std::endl;
  for (const auto& pair : metric.parts()) {
    const std::string& name = pair.first;
    const MetricPart& part = pair.second;
    std::string data_type;
    switch (part.data_type()) {
      case MetricPart::STRING:
        data_type = "string";
        break;

      case MetricPart::INT:
        data_type = "int";
        break;

      case MetricPart::BLOB:
        data_type = "blob";
        break;

      default:
        data_type = "";
    }
    *ostream_ << "One " << data_type << " part named \"" << name
              << "\": " << part.description() << std::endl;
  }
}

void TestApp::ShowEncodingConfig(const EncodingConfig& encoding) {
  switch (encoding.config_case()) {
    case EncodingConfig::kForculus:
      ShowForculusConfig(encoding.forculus());
      return;

    case EncodingConfig::kRappor:
      ShowRapporConfig(encoding.rappor());
      return;

    case EncodingConfig::kBasicRappor:
      ShowBasicRapporConfig(encoding.basic_rappor());
      return;

    case EncodingConfig::kNoOpEncoding:
      *ostream_ << "NoOp encoding";
      return;

    case EncodingConfig::CONFIG_NOT_SET:
      *ostream_ << "Invalid Encoding!";
      return;
  }
}

void TestApp::ShowForculusConfig(const ForculusConfig& config) {
  *ostream_ << "Forculus threshold=" << config.threshold() << std::endl;
}

void TestApp::ShowRapporConfig(const RapporConfig& config) {
  *ostream_ << "String Rappor" << std::endl;
}

void TestApp::ShowBasicRapporConfig(const BasicRapporConfig& config) {
  *ostream_ << "Basic Rappor " << std::endl;
  *ostream_ << "p=" << config.prob_0_becomes_1()
            << ", q=" << config.prob_1_stays_1() << std::endl;
  *ostream_ << "Categories:" << std::endl;
  switch (config.categories_case()) {
    case BasicRapporConfig::kStringCategories: {
      for (const std::string& s : config.string_categories().category()) {
        *ostream_ << s << std::endl;
      }
      return;
    }
    case BasicRapporConfig::kIntRangeCategories: {
      *ostream_ << config.int_range_categories().first() << " - "
                << config.int_range_categories().last();
      return;
    }
    case BasicRapporConfig::CATEGORIES_NOT_SET:
      *ostream_ << "Invalid Encoding!";
      return;
  }
}

bool TestApp::ParseInt(const std::string& str, bool complain, int64_t* x) {
  CHECK(x);
  std::istringstream iss(str);
  *x = 0;
  iss >> *x;
  char c;
  if (*x == 0 || iss.fail() || iss.get(c)) {
    if (complain) {
      if (mode_ == kInteractive) {
        *ostream_ << "Expected positive integer instead of " << str << "."
                  << std::endl;
      } else {
        LOG(ERROR) << "Expected positive integer instead of " << str;
      }
    }
    return false;
  }
  return true;
}

// Parses a string of the form <part>:<value>:<encoding> and writes <part> into
// |part_name| and <value> into |value| and <encoding> into encoding_config_id.
// Returns true if and only if this succeeds.
bool TestApp::ParsePartValueEncodingTriple(const std::string& triple,
                                           std::string* part_name,
                                           std::string* value,
                                           uint32_t* encoding_config_id) {
  CHECK(part_name);
  CHECK(value);
  if (triple.size() < 5) {
    return false;
  }
  auto last_pos = triple.size() - 1;

  auto index1 = triple.find(':');
  if (index1 == std::string::npos || index1 == 0 || index1 > last_pos - 3) {
    return false;
  }
  auto index2 = triple.find(':', index1 + 2);
  if (index2 == std::string::npos || index2 > last_pos - 1) {
    return false;
  }
  *part_name = std::string(triple, 0, index1);
  *value = std::string(triple, index1 + 1, index2 - index1 - 1);
  std::string int_string = std::string(triple, index2 + 1);
  int64_t id;
  if (!ParseInt(int_string, true, &id)) {
    return false;
  }
  if (id < 0) {
    if (mode_ == kInteractive) {
      *ostream_ << "<encoding> must be positive: " << id << std::endl;
    } else {
      LOG(ERROR) << "<encoding> must be positive: " << id;
    }
    return false;
  }
  *encoding_config_id = id;
  return true;
}

// Determines whether or not |str| is a triple of the kind that may be
// parsed by ParsePartValueEncodingTriple.
bool TestApp::IsTriple(const std::string str) {
  std::string part_name;
  std::string value;
  uint32_t encoding_config_id;
  return ParsePartValueEncodingTriple(str, &part_name, &value,
                                      &encoding_config_id);
}

}  // namespace cobalt
