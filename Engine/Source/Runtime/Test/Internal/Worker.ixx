module Nyx.Test:Worker;

import std;
import Nyx.Core;
import :Diagnostics;
import :Execution;
import :Policies;
import :Task;

namespace Nyx::Test::detail {

/// Environment-encoded request used when the current executable is acting as a worker.
struct WorkerRequest final {
  Path resultPath;
  Path faultPath;
  /// Stable identity used to select the filtered case after the child rebuilds the full reflected plan.
  String identifier;
  usize plannedCase{};
  usize runIteration{};
  u64 runSeed{};
  TimeMode timeMode{TimeMode::Real};
  TraceMode traceMode{TraceMode::Annotations};
  bool captureMemory{};
};

/// Partial journal state read after a worker exits normally or by native fault.
struct WorkerJournalResult final {
  Vec<TestExecution> executions;
  Option<AttemptIndex> activeAttempt;
  bool activeWarmup{};
  bool completed{};
};

/// Appends attempt boundaries and completed executions to a worker journal.
class WorkerJournal final {
public:
  explicit WorkerJournal(const Path &path) noexcept;
  ~WorkerJournal() noexcept;

  WorkerJournal(const WorkerJournal &) = delete ("WorkerJournal owns active file stream");
  auto operator=(const WorkerJournal &) -> WorkerJournal & = delete ("WorkerJournal owns active file stream");
  WorkerJournal(WorkerJournal &&) noexcept = delete ("WorkerJournal owns active file stream");
  auto operator=(WorkerJournal &&) noexcept
      -> WorkerJournal & = delete ("WorkerJournal owns active file stream");

  [[nodiscard]] auto ready() const noexcept -> bool;

  auto attemptStarted(AttemptIndex attempt, bool warmup) noexcept -> void;

  auto attemptCompleted(const TestExecution &execution) noexcept -> void;

  auto complete() noexcept -> void;

private:
  UPtr<std::ofstream> output_;
  bool ready_{};
};

/// Consumes the one worker request inherited from the parent process.
[[nodiscard]] auto consumeWorkerRequest() -> Option<WorkerRequest>;

/// Reads all complete journal records, preserving attempts completed before a worker fault.
[[nodiscard]] auto readWorkerJournal(const Path &path, const TestDescriptor &fallback) -> WorkerJournalResult;

/// Writes a diagnostic-safe worker failure when the journal cannot be opened or decoded.
[[nodiscard]] auto workerProtocolDiagnostic(StringView message, std::source_location location) -> Diagnostic;

} // namespace Nyx::Test::detail
