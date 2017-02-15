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

#include "analyzer/report_master/report_executor.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "glog/logging.h"

namespace cobalt {
namespace analyzer {

using store::ReportStore;

namespace {
// If the worker queue grows larger than this we will stop accepting new
// Enqueue requests.
const size_t kMaxQueueSize = 50000;

// Checks that report_id_chain is not empty and contains only complete
// ReportIds.
grpc::Status CheckReportIdChain(const std::vector<ReportId>& report_id_chain) {
  if (report_id_chain.empty()) {
    LOG(ERROR) << "report_id_chain is empty";
    return grpc::Status(grpc::INVALID_ARGUMENT, "report_id_chain is empty");
  }
  for (const ReportId& report_id : report_id_chain) {
    // When a client first creates a ReportId it is incomplete because
    // instance_id and creation_time_seconds are not set. These values are only
    // set by virtue of the client invoking ReportStore::StartNewReport(),
    // thereby creating a complete ReportId.
    if (report_id.instance_id() == 0 ||
        report_id.creation_time_seconds() == 0) {
      std::ostringstream stream;
      stream << "Not a complete ReportId: " << ReportStore::ToString(report_id);
      std::string message = stream.str();
      LOG(ERROR) << message;
      return grpc::Status(grpc::INVALID_ARGUMENT, message);
    }
  }
  return grpc::Status::OK;
}

}  // namespace

ReportExecutor::ReportExecutor(
    std::shared_ptr<store::ReportStore> report_store,
    std::unique_ptr<ReportGenerator> report_generator)
    : report_store_(report_store),
      report_generator_(std::move(report_generator)),
      shut_down_(false) {}

void ReportExecutor::Start() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // We set idle_ to false since we are about to start the worker thread.
    // The worker thread will set idle_ to true just before it becomes
    // idle.
    idle_ = false;
  }
  std::thread t([this] { this->Run(); });
  worker_thread_ = std::move(t);
}

ReportExecutor::~ReportExecutor() {
  if (!worker_thread_.joinable()) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    shut_down_ = true;
  }
  worker_notifier_.notify_all();
  worker_thread_.join();
}

grpc::Status ReportExecutor::EnqueueReportGeneration(
    std::vector<ReportId> report_id_chain) {
  auto status = CheckReportIdChain(report_id_chain);
  if (!status.ok()) {
    return status;
  }

  status = CheckQueueSize();
  if (!status.ok()) {
    return status;
  }

  return Enqueue(std::move(report_id_chain));
}

grpc::Status ReportExecutor::CheckQueueSize() {
  bool too_long = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    too_long = work_queue_.size() >= kMaxQueueSize;
  }
  if (too_long) {
    LOG(ERROR) << "Work queue too long!";
    return grpc::Status(grpc::ABORTED,
                        "Can't enqueue reports: queue too long!");
  }
  return grpc::Status::OK;
}

grpc::Status ReportExecutor::Enqueue(std::vector<ReportId> report_id_chain) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shut_down_) {
      std::string message = "Shutting down. Not enqueuing.";
      LOG(ERROR) << message;
      return grpc::Status(grpc::ABORTED, message);
    }
    work_queue_.emplace_back(std::move(report_id_chain));
    // Set idle_ false because any thread that invokes WaitUntilIdle() after
    // this should wait until the |report_id_chain| just enqueued is
    // processed.
    idle_ = false;
  }
  worker_notifier_.notify_all();
  return grpc::Status::OK;
}

void ReportExecutor::Run() {
  while (!shut_down_) {
    std::vector<ReportId> dependency_chain;
    if (!WaitAndTakeFirst(&dependency_chain)) {
      return;
    }
    ProcessDependencyChain(dependency_chain);
  }
}

bool ReportExecutor::WaitAndTakeFirst(std::vector<ReportId>* chain_out) {
  CHECK(chain_out);
  std::unique_lock<std::mutex> lock(mutex_);
  if (shut_down_) {
    return false;
  }
  if (work_queue_.empty()) {
    // Notify observers that the the worker thread is now idle;
    idle_ = true;
    idle_notifier_.notify_all();

    // Wait until the condition variable is notified and either shut_down_
    // is set or the work_queue_ is not empty.
    worker_notifier_.wait(lock, [this] {
      return (this->shut_down_ || !this->work_queue_.empty());
    });
  }
  idle_ = false;
  if (shut_down_) {
    return false;
  }
  CHECK(!work_queue_.empty());
  chain_out->swap(work_queue_[0]);
  work_queue_.pop_front();
  return true;
}

void ReportExecutor::WaitUntilIdle() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (idle_) {
    return;
  }
  // Wait until the condition variable is notified and idle_ is true.
  idle_notifier_.wait(lock, [this] { return (this->idle_); });
}

void ReportExecutor::ProcessDependencyChain(
    const std::vector<ReportId>& chain) {
  DCHECK(!chain.empty());
  bool chain_failed = false;
  for (const ReportId& report_id : chain) {
    if (shut_down_) {
      LOG(INFO) << "Shutting down.";
      return;
    }
    if (chain_failed) {
      std::ostringstream stream;
      stream << "Skipping report generation for report_id="
             << ReportStore::ToString(report_id)
             << " because an earlier report in its dependency chain failed.";
      std::string message = stream.str();
      LOG(ERROR) << message;
      EndReport(report_id, false, message);
    } else {
      chain_failed = !ProcessReportId(report_id);
    }
  }
}

bool ReportExecutor::ProcessReportId(const ReportId& report_id) {
  ReportMetadataLite metadata;
  if (!GetMetadata(report_id, &metadata)) {
    EndReport(report_id, false, "Unable to fetch metadata for report.");
    return false;
  }

  switch (metadata.state()) {
    case WAITING_TO_START: {
      if (!StartSecondarySlice(report_id)) {
        EndReport(report_id, false, "Unable to start secondary slice.");
        return false;
      }
      break;
    }

    case IN_PROGRESS:
      break;

    default: {
      // Already in a terminal state.
      LOG(ERROR) << "Unexpected state: " << metadata.state()
                 << " for report_id=" << ReportStore::ToString(report_id);
      return false;
    }
  }

  auto status = report_generator_->GenerateReport(report_id);
  std::string message = (status.ok() ? "" : status.error_message());

  // End the report and then return true only if both GenerateReport and
  // EndReport succeeded.
  return EndReport(report_id, status.ok(), message) && status.ok();
}

bool ReportExecutor::GetMetadata(const ReportId& report_id,
                                 ReportMetadataLite* metadata_out) {
  auto status = report_store_->GetMetadata(report_id, metadata_out);
  if (status != store::kOK) {
    LOG(ERROR) << "GetMetadata failed with status=" << status
               << " for report_id=" << ReportStore::ToString(report_id);
    return false;
  }
  return true;
}

bool ReportExecutor::StartSecondarySlice(const ReportId& report_id) {
  auto status = report_store_->StartSecondarySlice(report_id);
  if (status != store::kOK) {
    LOG(ERROR) << "StartSecondarySlice failed with status=" << status
               << " for report_id=" << ReportStore::ToString(report_id);
    return false;
  }
  return true;
}

bool ReportExecutor::EndReport(const ReportId& report_id, bool success,
                               std::string message) {
  auto status =
      report_store_->EndReport(report_id, success, std::move(message));
  if (status != store::kOK) {
    LOG(ERROR) << "EndReport failed with status=" << status
               << " for report_id=" << ReportStore::ToString(report_id);
    return false;
  }

  return true;
}

}  // namespace analyzer
}  // namespace cobalt
