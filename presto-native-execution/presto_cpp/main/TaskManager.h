/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <folly/Synchronized.h>
#include <memory>
#include "presto_cpp/main/PrestoTask.h"
#include "presto_cpp/main/QueryContextManager.h"
#include "presto_cpp/main/http/HttpServer.h"
#include "presto_cpp/presto_protocol/presto_protocol.h"
#include "velox/exec/PartitionedOutputBufferManager.h"

namespace facebook::presto {

struct DriverCountStats {
  size_t numBlockedDrivers{0};
  size_t numRunningDrivers{0};
};

class TaskManager {
 public:
  TaskManager();

  void setBaseUri(const std::string& baseUri) {
    baseUri_ = baseUri;
  }

  void setNodeId(const std::string& nodeId) {
    nodeId_ = nodeId;
  }

  TaskMap tasks() const {
    return taskMap_.withRLock([](const auto& tasks) { return tasks; });
  }

  void abortResults(const protocol::TaskId& taskId, long bufferId);

  void
  acknowledgeResults(const protocol::TaskId& taskId, long bufferId, long token);

  // Creating an empty task that only contains the error information so that
  // next time coordinator checks for the status it retrieves the error.
  std::unique_ptr<protocol::TaskInfo> createOrUpdateErrorTask(
      const protocol::TaskId& taskId,
      const std::exception_ptr& exception);

  std::unique_ptr<protocol::TaskInfo> createOrUpdateTask(
      const protocol::TaskId& taskId,
      const protocol::TaskUpdateRequest& updateRequest,
      const velox::core::PlanFragment& planFragment,
      std::shared_ptr<velox::core::QueryCtx> queryCtx);

  std::unique_ptr<protocol::TaskInfo> createOrUpdateBatchTask(
      const protocol::TaskId& taskId,
      const protocol::BatchTaskUpdateRequest& batchUpdateRequest,
      const velox::core::PlanFragment& planFragment,
      std::shared_ptr<velox::core::QueryCtx> queryCtx);

  // Iterates through a map of resultRequests and fetches data from
  // buffer manager. This method uses the getData() global call to fetch
  // data for each resultRequest bufferManager. If the output buffer for task
  // is not found, prepares the Result object with completed flags set to false
  // and notifies the future.
  // Note: This method is made public for unit testing purpose only.
  void getDataForResultRequests(
      const std::unordered_map<int64_t, std::shared_ptr<ResultRequest>>&
          resultRequests);

  std::unique_ptr<protocol::TaskInfo> deleteTask(
      const protocol::TaskId& taskId,
      bool abort);

  /// Remove old Finished, Cancelled, Failed and Aborted tasks.
  /// Old is being defined by the lifetime of the task.
  size_t cleanOldTasks();

  /// Invoked by Presto server shutdown to wait for all the tasks to complete.
  void waitForTasksToComplete();

  folly::Future<std::unique_ptr<protocol::TaskInfo>> getTaskInfo(
      const protocol::TaskId& taskId,
      bool summarize,
      std::optional<protocol::TaskState> currentState,
      std::optional<protocol::Duration> maxWait,
      std::shared_ptr<http::CallbackRequestHandlerState> state);

  folly::Future<std::unique_ptr<Result>> getResults(
      const protocol::TaskId& taskId,
      long bufferId,
      long token,
      protocol::DataSize maxSize,
      protocol::Duration maxWait,
      std::shared_ptr<http::CallbackRequestHandlerState> state);

  folly::Future<std::unique_ptr<protocol::TaskStatus>> getTaskStatus(
      const protocol::TaskId& taskId,
      std::optional<protocol::TaskState> currentState,
      std::optional<protocol::Duration> maxWait,
      std::shared_ptr<http::CallbackRequestHandlerState> state);

  void removeRemoteSource(
      const protocol::TaskId& taskId,
      const protocol::TaskId& remoteSourceTaskId);

  std::string toString() const;

  QueryContextManager* getQueryContextManager() {
    return &queryContextManager_;
  }

  /// Make upto target task threads to yield. Task candidate must have been on
  /// thread for at least sliceMicros to be yieldable. Return the number of
  /// threads in tasks that were requested to yield.
  int32_t yieldTasks(int32_t numTargetThreadsToYield, int32_t timeSliceMicros);

  const QueryContextManager* getQueryContextManager() const {
    return &queryContextManager_;
  }

  inline size_t getNumTasks() const {
    return taskMap_.rlock()->size();
  }

  // Returns the number of running drivers in all tasks.
  DriverCountStats getDriverCountStats() const;

  // Returns array with number of tasks for each of five TaskState (enum defined
  // in exec/Task.h).
  std::array<size_t, 5> getTaskNumbers(size_t& numTasks) const;

  /// Build directory path for spilling for the given task.
  /// Always returns non-empty string.
  static std::string buildTaskSpillDirectoryPath(
      const std::string& baseSpillPath,
      const std::string& nodeIp,
      const std::string& nodeId,
      const std::string& queryId,
      const protocol::TaskId& taskId);

 public:
  static constexpr folly::StringPiece kMaxDriversPerTask{
      "max_drivers_per_task"};
  static constexpr folly::StringPiece kConcurrentLifespansPerTask{
      "concurrent_lifespans_per_task"};
  static constexpr folly::StringPiece kSessionTimezone{"session_timezone"};

 private:
  std::unique_ptr<protocol::TaskInfo> createOrUpdateTask(
      const protocol::TaskId& taskId,
      const velox::core::PlanFragment& planFragment,
      const std::vector<protocol::TaskSource>& sources,
      const protocol::OutputBuffers& outputBuffers,
      std::shared_ptr<velox::core::QueryCtx> queryCtx);

  std::shared_ptr<PrestoTask> findOrCreateTask(const protocol::TaskId& taskId);

  std::shared_ptr<PrestoTask> findOrCreateTaskLocked(
      TaskMap& taskMap,
      const protocol::TaskId& taskId);

  std::string baseUri_;
  std::string nodeId_;
  std::shared_ptr<velox::exec::PartitionedOutputBufferManager> bufferManager_;
  folly::Synchronized<TaskMap> taskMap_;
  QueryContextManager queryContextManager_;
};

} // namespace facebook::presto
