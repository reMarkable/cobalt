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

#ifndef COBALT_ANALYZER_REPORT_MASTER_REPORT_EXECUTOR_H_
#define COBALT_ANALYZER_REPORT_MASTER_REPORT_EXECUTOR_H_

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "analyzer/report_master/report_generator.h"
#include "analyzer/report_master/report_internal.pb.h"
#include "analyzer/store/report_store.h"
#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {

// ReportExecutor is an asynchronous work executor for Cobalt report
// generation. The caller enqueues ReportIds and ReportExecutor
// will eventually generate the report with the given ReportId.
//
// ReportExecutor delegates to an instance of ReportGenerator to perform
// the actual report generation.
//
// ReportExecutor records the success or failure of the generation attempts in
// the ReportStore and querying the ReportStore is how the caller discovers
// the state of a report after its ID has been enqueued. ReportExecutor offers
// no direct way to obtain information about a report after its ID has been
// enqueued.
//
// ReportExecutor offers the ability to enqueue not just a single ReportId but
// a *dependency chain* of ReportIds. This is a sequence of ReportIds in which
// each ID in the sequence depends on the previous one. The reports in a
// dependency chain are guaranteed to be generated sequentially in the order of
// the chain, and iteration through the chain stops as soon as one of the report
// generations fails. An example of where we use this feature is in the
// handling of joint two-variable reports. When ReportMaster wants to
// generate a joint report it first generates the two one-variable marginal
// reports. This is implemented by creating a dependency chain that includes
// first the two marginal reports followed by the joint report.
//
// The current version of the implementation is very simple: A single work queue
// and a single worker thread are used.
class ReportExecutor {
 public:
  // Constructs a ReportExecutor that reads and writes from the given
  // |report_store| and delegates to the given |report_generator|.
  ReportExecutor(std::shared_ptr<store::ReportStore> report_store,
                 std::unique_ptr<ReportGenerator> report_generator);

  // The destructor will stop the worker thread and wait for it to stop
  // before exiting. But it is the responsibility of the client of this
  // class to ensure that there are no concurrent invocations of
  // EnqueueReportGeneration() or WaitUntilIdle().
  ~ReportExecutor();

  // Starts the worker thread. Destruct this object to stop the worker thread.
  // This method must be invoked exactly once.
  void Start();

  // Enqueues a dependency chain of ReportIds of reports to be generated.
  //
  // Each of the ReportIds given must be a complete ID as returned from
  // ReportStore::StartNewReport or ReportStore::StartDependentReport.
  // ReportExecutor will query the metadata for each ReportId from the
  // ReportStore. The metadata must exist and the report must currently be in
  // either the WAITING_TO_START state or the IN_PROGRESS state.
  //
  // After a dependency chain of ReportIds is enqueued, eventually
  // ReportExecutor will attempt to  generate the reports in the
  // dependency chain by iterating through the ReportIds in the chain and
  // invoking ReportGenerator::GenerateReport on each ReportId.
  //
  // The reports in the chain will be generated sequentially in the order given
  // by the chain. As soon as ReportGenerator::GenerateReport() returns a
  // non-success status for one of the ReportIds in the chain, the rest of the
  // chain will be abandoned and ReportExecutor will move on to the next
  // dependency chain that was enqueued.
  //
  // ReportExecutor uses the ReportStore to discover and record the current
  // state of report generation for each report. If a report is in the
  // WAITING_TO_START state then before invoking
  // ReportGenerator::GenerateReport() ReportExecutor will invoke
  // ReportStore::StartDependentReport() in order to put the report into the
  // IN_PROGRESS_STATE. After GenerateReport() returns, ReportExecutor will
  // invoke ReportStore::EndReport() in order to put the report into either
  // the COMPLETED_SUCCESSFULLY state or the TERMINATED state as appropriate.
  // If a report is never generated because it is part of a dependency chain
  // and an earlier report in the chain failed, then the report will be put
  // into the TERMINATED state. A human-readable message will be added to the
  // info_messages field of the report metadata describing why the report
  // was TERMINATED.
  //
  // Returns grpc::Status::OK if all ReportIds are valid and were successfully
  // enqueued, or an error Status otherwise. In particular returns
  // grpc::Status::INVALID_ARGUMENT if |report_id_chain| is empty or if it
  // contains an invalid ReportId. Returns grpc::Status::ABORTED if the queue is
  // already too long.
  grpc::Status EnqueueReportGeneration(std::vector<ReportId> report_id_chain);

  // Blocks until the worker thread is idle, meaning that the work queue is
  // empty and the worker thread has finished processing all previously
  // enqueued reports and it is waiting for another invocation of
  // EnqueueReportGeneration(). Returns immediately if Start() was never
  // invoked.
  void WaitUntilIdle();

 private:
  // If the queue is too long, logs an error message and returns an error
  // status. Otherwise returns OK.
  grpc::Status CheckQueueSize();

  // Adds |chain| to the end of |work_queue_|. Returns OK on success or
  // an error status.
  grpc::Status Enqueue(std::vector<ReportId> report_id_chain);

  // The main function that runs in the ReportExecutor's worker thread.
  // Repeatedly dequeues and processes dependency chains of ReportIds.
  // Exits when shut_down_ is set true.
  void Run();

  // Waits for the work_queue_ to be non-empty or for shut_down_ to be
  // true. If the work_queue_ is non-empty then pops the first element
  // from the work_queue_ and swaps it into |chain_out| and returns true.
  // If shut_down_ is true then returns false.
  bool WaitAndTakeFirst(std::vector<ReportId>* chain_out);

  // Iterates through the ReportIds in |chain| and invokes
  // ProcessReportId() until one of the reports fail
  // or shut_down_ is set true.
  void ProcessDependencyChain(const std::vector<ReportId>& chain);

  // Attempts to get the metadata for |report_id|, invoke
  // ReportGenerator::StartDependentReport() if necessary, invoke
  // ReportGenerator::GenerateReport(), and invoke
  // ReportStore::EndReport() to mark the report as completed either
  // successfully or unsuccessfully as appropriate. Logs an error message and
  // returns false on error or returns true on success.
  bool ProcessReportId(const ReportId& report_id);

  // Invokes ReportStore::GetMetadata. On success returns true. On error logs
  // a message and returns false.
  bool GetMetadata(const ReportId& report_id, ReportMetadataLite* metadata_out);

  // Invokes ReportGenerator::StartDependentReport(). On success returns true.
  // On error logs a message, sets message_out and returns false.
  bool StartDependentReport(const ReportId& report_id);

  // Invokes ReportStore::EndReport. On success returns true. On error logs
  // a message and returns false.
  bool EndReport(const ReportId& report_id, bool success, std::string message);

  std::shared_ptr<store::ReportStore> report_store_;
  std::unique_ptr<ReportGenerator> report_generator_;

  // The "Run()" method runs in this thread.
  std::thread worker_thread_;

  // Set shut_down to true in order to stop "Run()".
  std::atomic<bool> shut_down_;

  // Is the worker thread in the idle state? Set to true initially since the
  // worker thread has not been started. Protected by mutex_.
  bool idle_ = true;

  // Protects access to work_queue_ and idle_.
  std::mutex mutex_;

  // Notifies the sleeping worker thread when an Enqueue has occurred or
  // shut_down_ has been set true. Uses mutex_.
  std::condition_variable worker_notifier_;

  // Notifies threads that have called WaitUntilIdle(). Uses mutex_.
  std::condition_variable idle_notifier_;

  // Protected by mutex_.
  std::deque<std::vector<ReportId>> work_queue_;
};

}  // namespace analyzer
}  // namespace cobalt

#endif  // COBALT_ANALYZER_REPORT_MASTER_REPORT_EXECUTOR_H_
