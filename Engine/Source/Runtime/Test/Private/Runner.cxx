module Nyx.Test;

import :Runner;
import :FaultIsolation;
import :Worker;

import std;
import Nyx.Core;

namespace Nyx::Test::detail {

namespace {

struct ScheduledCase final {
  usize plannedCase{};
  usize runIteration{};
};

/// Serializes repeated invocations whose factory has an explicit shared fixture capability.
///
/// Different planned cases retain full worker parallelism. Repetitions of one case use one lane when its
/// factory has shared fixture state and is not concurrently invocable.
///
/// FixtureScope remains safe to initialize concurrently, but serializing fixture-backed cases preserves
/// established ordering for fixture construction and user code that exposes mutable fixture substate.
struct ExecutionLane final {
  [[nodiscard]] auto mutex() noexcept -> std::mutex & {
    return mutex_;
  }

private:
  std::mutex mutex_;
};

/// Carries every value needed to execute one scheduled case.
///
/// The scheduler resolves the planned-case index once and stores the effective isolation policy here. The
/// lower-level attempt and worker functions therefore consume one coherent plan instead of independently
/// combining a case reference, capabilities, index, schedule entry, options, seed, and resource lane.
/// All references in this value are non-owning. The dispatch context and optional worker journal must outlive
/// the plan and every operation that consumes it.
struct InvocationPlan final {
  Ref<const PlannedCase> plannedCase;
  InvocationCapabilities capabilities{};
  usize plannedCaseIndex{};
  ScheduledCase scheduledCase{};
  Ref<const RunOptions> options;
  u64 runSeed{};
  CrashIsolation isolation{};
  bool captureMemory{};
  Option<Ref<ExecutionLane>> lane;
  Option<Ref<WorkerJournal>> journal;

  [[nodiscard]] constexpr auto processIsolated() const noexcept -> bool {
    return isolation == CrashIsolation::ProcessPerCase;
  }
};

class SplitMix64 final {
public:
  explicit constexpr SplitMix64(u64 seed) noexcept
      : state_(seed) {
  }

  [[nodiscard]] constexpr auto next() noexcept -> u64 {
    state_ += increment_;
    u64 value = state_;
    value = (value ^ (value >> firstShift_)) * firstMultiplier_;
    value = (value ^ (value >> secondShift_)) * secondMultiplier_;
    return value ^ (value >> finalShift_);
  }

private:
  static constexpr u64 increment_{0x9E3779B97F4A7C15ULL};
  static constexpr u64 firstMultiplier_{0xBF58476D1CE4E5B9ULL};
  static constexpr u64 secondMultiplier_{0x94D049BB133111EBULL};
  static constexpr u32 firstShift_{30};
  static constexpr u32 secondShift_{27};
  static constexpr u32 finalShift_{31};

  u64 state_{};
};

[[nodiscard]] auto randomSeed() -> u64 {
  constexpr u32 halfSeedWidth{32};
  std::random_device device{};
  const u64 high = static_cast<u64>(device());
  const u64 low = static_cast<u64>(device());
  const u64 clock = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
  SplitMix64 mixer{(high << halfSeedWidth) ^ low ^ clock};
  return mixer.next();
}

[[nodiscard]] constexpr auto deriveSeed(u64 runSeed, usize plannedCase, AttemptIndex attempt, bool warmup)
    -> u64 {
  constexpr u64 streamSalt{0xD1B54A32D192ED03ULL};
  SplitMix64 mixer{runSeed ^ streamSalt};
  const u64 caseSeed = mixer.next() ^ static_cast<u64>(plannedCase);
  const u64 runSeedPart = mixer.next() ^ static_cast<u64>(attempt.runIteration);
  const u64 sampleSeed = mixer.next() ^ static_cast<u64>(attempt.sample);
  const u64 retrySeed = mixer.next() ^ static_cast<u64>(attempt.retry);
  const u64 warmupSeed = mixer.next() ^ static_cast<u64>(warmup);
  SplitMix64 result{caseSeed ^ runSeedPart ^ sampleSeed ^ retrySeed ^ warmupSeed};
  return result.next();
}

[[nodiscard]] auto scheduleCases(usize plannedCaseCount, const RunOptions &options, u64 runSeed)
    -> Vec<ScheduledCase> {
  if (options.repeat == 0)
    fatal("Nyx::Test RunOptions::repeat must be greater than zero");

  Vec<ScheduledCase> scheduled{};
  scheduled.reserve(plannedCaseCount * options.repeat);
  std::ranges::for_each(
      std::views::indices(options.repeat), [&scheduled, plannedCaseCount](usize iteration) -> void {
        std::ranges::for_each(
            std::views::indices(plannedCaseCount), [&scheduled, iteration](usize plannedCase) -> void {
              scheduled.push_back(ScheduledCase{
                  .plannedCase = plannedCase,
                  .runIteration = iteration,
              });
            });
      });

  if (options.order == ExecutionOrder::Declaration or scheduled.size() < 2)
    return scheduled;

  SplitMix64 random{runSeed};
  std::ranges::for_each(std::views::iota(1U, scheduled.size()) | std::views::reverse,
      [&scheduled, &random](usize index) -> void {
        const auto selected = static_cast<usize>(random.next() % (index + 1));
        std::ranges::swap(scheduled[index], scheduled[selected]);
      });
  return scheduled;
}

[[nodiscard]] constexpr auto forceTrace(TraceMode traceMode) noexcept -> bool {
  return traceMode != TraceMode::Annotations;
}

[[nodiscard]] auto boundaryFailure(TestDescriptor descriptor, String message) -> TestExecution {
  TestExecution execution{
      .descriptor = std::move(descriptor),
  };
  execution.state.diagnostics.push_back(
      unhandledExceptionDiagnostic(std::move(message), execution.descriptor.location));
  execution.state.errors = 1;
  return execution;
}

[[nodiscard]] auto workerFailure(TestDescriptor descriptor, NativeFault fault, std::source_location location)
    -> TestExecution {
  TestExecution execution{
      .descriptor = std::move(descriptor),
      .fault = fault,
  };
  execution.state.diagnostics.push_back(nativeFaultDiagnostic(fault, location));
  execution.state.errors = 1;
  return execution;
}

[[nodiscard]] auto workerFailure(TestDescriptor descriptor, Diagnostic diagnostic) -> TestExecution {
  TestExecution execution{
      .descriptor = std::move(descriptor),
  };
  execution.state.diagnostics.push_back(std::move(diagnostic));
  execution.state.errors = 1;
  return execution;
}

auto executeAttempt(const InvocationPlan &plan, AttemptIndex attempt, bool warmup) -> TestExecution {
  const RunOptions &options = plan.options.get();
  const InvocationSettings settings{
      .seed = deriveSeed(plan.runSeed, plan.plannedCaseIndex, attempt, warmup),
      .iteration = plan.scheduledCase.runIteration,
      .sample = attempt.sample,
      .retry = attempt.retry,
      .warmup = warmup,
      .forceTrace = forceTrace(options.traceMode),
      .captureMemory = plan.captureMemory,
  };

  const InvocationBinding binding{settings};
  TestExecution execution{};
  const PlannedCase &plannedCase = plan.plannedCase.get();

  try {
    execution = plannedCase.invoke(InvocationRequest(plannedCase.descriptor(), options.timeMode));
  } catch (const TestAbort &) {
    execution = TestExecution{
        .descriptor = TestDescriptor{plannedCase.descriptor()},
    };
    execution.state.aborted = true;
  } catch (const std::exception &exception) {
    const char *message = exception.what();
    execution = boundaryFailure(
        TestDescriptor{plannedCase.descriptor()}, message == nullptr ? "standard exception" : message);
  } catch (...) {
    execution = boundaryFailure(TestDescriptor{plannedCase.descriptor()}, "non-standard exception");
  }
  execution.runSeed = plan.runSeed;
  execution.attempt = attempt;
  execution.iteration = plan.scheduledCase.runIteration;
  execution.warmup = warmup;
  execution.traceMode = options.traceMode;
  return execution;
}

[[nodiscard]] constexpr auto hasTimeout(const TestExecution &execution) noexcept -> bool {
  const bool hasTimeoutDiagnostic = std::ranges::any_of(
      execution.state.diagnostics, [](const Diagnostic &diagnostic) constexpr noexcept -> bool {
        return diagnostic.header.code == DiagnosticCode::TimeoutExceeded;
      });
  if (not hasTimeoutDiagnostic)
    return false;

  return std::ranges::all_of(
      execution.state.diagnostics, [](const Diagnostic &diagnostic) constexpr noexcept -> bool {
        return diagnostic.header.code == DiagnosticCode::TimeoutExceeded;
      });
}

[[nodiscard]] constexpr auto sameSample(const TestExecution &left, const TestExecution &right) noexcept
    -> bool {
  return not left.warmup and not right.warmup and left.attempt.runIteration == right.attempt.runIteration and
         left.attempt.sample == right.attempt.sample;
}

[[nodiscard]] auto batchFailed(const Vec<TestExecution> &executions) noexcept -> bool {
  return std::ranges::any_of(executions | std::views::enumerate,
      [&executions](const Tuple<usize, const TestExecution &> &item) -> bool {
        const auto &[index, execution] = item;
        if (execution.warmup)
          return execution.failed();

        if (execution.passed())
          return false;

        if (not hasTimeout(execution))
          return true;

        const auto recovered = std::ranges::find_if(executions.begin() + static_cast<isize>(index) + 1,
            executions.end(),
            [&execution](const TestExecution &candidate) -> bool {
              return sameSample(execution, candidate) and
                     candidate.attempt.retry > execution.attempt.retry and candidate.passed();
            });
        return recovered == executions.end();
      });
}

class CaseExecutor final {
public:
  explicit CaseExecutor(const InvocationPlan &plan)
      : plan_(plan) {
  }

  [[nodiscard]] auto run() -> Vec<TestExecution> {
    std::unique_lock<std::mutex> lock{};
    const InvocationPlan &plan = plan_.get();
    if (plan.lane)
      lock = std::unique_lock<std::mutex>{plan.lane->get().mutex()};

    runWarmups();
    runSamples();
    return std::move(executions_);
  }

private:
  auto appendAttempt(AttemptIndex attempt, bool warmup) -> const TestExecution & {
    const InvocationPlan &plan = plan_.get();
    if (plan.journal)
      plan.journal->get().attemptStarted(attempt, warmup);

    executions_.push_back(executeAttempt(plan_, attempt, warmup));
    if (plan.journal)
      plan.journal->get().attemptCompleted(executions_.back());
    return executions_.back();
  }

  auto runWarmups() -> void {
    const InvocationPlan &plan = plan_.get();
    const usize warmups = plan.plannedCase.get().descriptor().policy.warmup;
    const usize runIteration = plan.scheduledCase.runIteration;

    std::ranges::for_each(std::views::indices(warmups), [this, runIteration](usize warmupIndex) -> void {
      static_cast<void>(appendAttempt(
          AttemptIndex{
              .runIteration = runIteration,
              .sample = warmupIndex,
          },
          true));
    });
  }

  auto runSamples() -> void {
    const InvocationPlan &plan = plan_.get();
    const usize samples = std::max(plan.plannedCase.get().descriptor().policy.repeat, 1UZ);
    std::ranges::for_each(std::views::indices(samples), [this](usize sample) -> void { runSample(sample); });
  }

  auto runSample(usize sample) -> void {
    const InvocationPlan &plan = plan_.get();
    usize retryIndex{};
    bool retrying{true};

    // The loop invariant is that every stored attempt for this sample has already completed. It exits after
    // the first non-timeout result or after the declared retry budget is exhausted.
    while (retrying) {
      const TestExecution &execution = appendAttempt(
          AttemptIndex{
              .runIteration = plan_.get().scheduledCase.runIteration,
              .sample = sample,
              .retry = retryIndex,
          },
          false);
      retrying = not execution.passed() and hasTimeout(execution) and
                 retryIndex < plan.plannedCase.get().descriptor().policy.retry;
      if (retrying)
        ++retryIndex;
    }
  }

  Ref<const InvocationPlan> plan_;
  Vec<TestExecution> executions_;
};

auto executeCase(const InvocationPlan &plan) -> Vec<TestExecution> {
  return CaseExecutor{plan}.run();
}

[[nodiscard]] auto laneFor(Vec<UPtr<ExecutionLane>> &executionLanes, usize plannedCase) noexcept
    -> Option<Ref<ExecutionLane>> {
  if (executionLanes.empty() or plannedCase >= executionLanes.size())
    return None;

  UPtr<ExecutionLane> &lane = executionLanes[plannedCase];
  if (lane == nullptr)
    return None;

  return std::ref(*lane);
}

[[nodiscard]] constexpr auto isolationFor(const PlannedCase &plannedCase, const RunOptions &options) noexcept
    -> CrashIsolation {
  if (plannedCase.descriptor().policy.parent)
    return CrashIsolation::InProcess;

  if (plannedCase.descriptor().policy.isolated)
    return CrashIsolation::ProcessPerCase;

  return options.isolation;
}

struct DispatchContext final { // NOLINT(cppcoreguidelines-pro-type-member-init)
  Ref<Vec<PlannedCase>> plannedCases;
  Ref<Vec<UPtr<ExecutionLane>>> executionLanes;
  Ref<const RunOptions> options;
  u64 runSeed{};
  bool captureMemory{};
};

[[nodiscard]] auto executeIsolatedCase(const InvocationPlan &plan) -> Vec<TestExecution>;

[[nodiscard]] auto makeInvocationPlan(DispatchContext &context, const ScheduledCase &scheduledCase)
    -> InvocationPlan {
  const PlannedCase &plannedCase = context.plannedCases.get()[scheduledCase.plannedCase];
  const CrashIsolation isolation = isolationFor(plannedCase, context.options.get());
  return InvocationPlan{
      .plannedCase = plannedCase,
      .capabilities = plannedCase.capabilities(),
      .plannedCaseIndex = scheduledCase.plannedCase,
      .scheduledCase = scheduledCase,
      .options = context.options,
      .runSeed = context.runSeed,
      .isolation = isolation,
      .captureMemory = context.captureMemory or isolation == CrashIsolation::ProcessPerCase,
      .lane = laneFor(context.executionLanes, scheduledCase.plannedCase),
  };
}

[[nodiscard]] auto executeScheduledCase(DispatchContext &context, const ScheduledCase &scheduledCase)
    -> Vec<TestExecution> {
  const InvocationPlan plan = makeInvocationPlan(context, scheduledCase);
  if (plan.processIsolated())
    return executeIsolatedCase(plan);

  return executeCase(plan);
}

[[nodiscard]] constexpr auto workerCount(usize requested, usize workCount) -> usize {
  if (workCount == 0)
    return 0;

  if (requested == 1)
    return 1;

  if (requested == 0) {
    const usize available = std::thread::hardware_concurrency();
    return std::min(std::max(available, 1UZ), workCount);
  }

  return std::min(requested, workCount);
}

[[nodiscard]] auto workerVariables(const WorkerRequest &request) -> Vec<Pair<String, String>> {
  return Vec<Pair<String, String>>{
      {"NYX_TEST_WORKER", "1"},
      {"NYX_TEST_WORKER_RESULT", request.resultPath.string()},
      {"NYX_TEST_WORKER_FAULT", request.faultPath.string()},
      {"NYX_TEST_WORKER_IDENTIFIER", request.identifier},
      {"NYX_TEST_WORKER_CASE", std::to_string(request.plannedCase)},
      {"NYX_TEST_WORKER_ITERATION", std::to_string(request.runIteration)},
      {"NYX_TEST_WORKER_SEED", std::to_string(request.runSeed)},
      {"NYX_TEST_WORKER_TIME", std::to_string(static_cast<u8>(request.timeMode))},
      {"NYX_TEST_WORKER_TRACE", std::to_string(static_cast<u8>(request.traceMode))},
      {"NYX_TEST_WORKER_MEMORY", request.captureMemory ? "1" : "0"},
  };
}

[[nodiscard]] auto workerPaths() -> Option<Pair<Path, Path>> {
  const Result<Path> root = fs::temporaryDirectory("nyx-worker");
  if (not root)
    return None;

  return Pair<Path, Path>{*root / "result.bin", *root / "fault.bin"};
}

auto removeWorkerFiles(const Path &resultPath, const Path &faultPath) noexcept -> void {
  std::error_code error{};
  static_cast<void>(std::filesystem::remove(resultPath, error));
  error.clear();
  static_cast<void>(std::filesystem::remove(faultPath, error));
  error.clear();
  static_cast<void>(std::filesystem::remove(resultPath.parent_path(), error));
}

auto appendWorkerFailure(Vec<TestExecution> &executions,
    const InvocationPlan &plan,
    const WorkerJournalResult &journal,
    TestExecution execution) -> void {
  if (journal.activeAttempt and not journal.activeWarmup) {
    const auto completed = std::ranges::find_if(
        executions, [&journal](const TestExecution &execution) constexpr noexcept -> bool {
          return execution.attempt == *journal.activeAttempt;
        });
    if (completed != executions.end()) {
      completed->fault = execution.fault;
      completed->state = std::move(execution.state);
      return;
    }
  }

  execution.runSeed = plan.runSeed;
  execution.iteration = plan.scheduledCase.runIteration;
  execution.attempt = journal.activeAttempt.value_or(AttemptIndex{
      .runIteration = plan.scheduledCase.runIteration,
  });
  execution.warmup = journal.activeWarmup;
  execution.seed = deriveSeed(plan.runSeed, plan.plannedCaseIndex, execution.attempt, execution.warmup);
  execution.traceMode = plan.options.get().traceMode;

  if (not execution.state.diagnostics.empty()) {
    Diagnostic &diagnostic = execution.state.diagnostics.front();
    const String status = execution.fault
                              ? std::format("native fault ({})", debug::enumName(execution.fault->kind))
                              : String{"protocol failure"};
    diagnostic.addNote(std::format("worker status: {}", status));
    diagnostic.addNote(std::format("test case: {}", execution.descriptor.identifier));
    diagnostic.addNote(std::format("attempt: run {}, sample {}, retry {}",
        execution.attempt.runIteration + 1,
        execution.attempt.sample + 1,
        execution.attempt.retry));
    diagnostic.addNote(std::format("seed: {}", execution.seed));
  }

  executions.push_back(std::move(execution));
}

auto appendWorkerFault(Vec<TestExecution> &executions,
    const InvocationPlan &plan,
    const WorkerJournalResult &journal,
    NativeFault fault) -> void {
  appendWorkerFailure(executions,
      plan,
      journal,
      workerFailure(
          plan.plannedCase.get().descriptor(), fault, plan.plannedCase.get().descriptor().location));
}

auto appendWorkerProtocolFailure(Vec<TestExecution> &executions,
    const InvocationPlan &plan,
    const WorkerJournalResult &journal,
    StringView message) -> void {
  appendWorkerFailure(executions,
      plan,
      journal,
      workerFailure(plan.plannedCase.get().descriptor(),
          workerProtocolDiagnostic(message, plan.plannedCase.get().descriptor().location)));
}

[[nodiscard]] auto unavailableWorker(const InvocationPlan &plan) -> Vec<TestExecution> {
  Vec<TestExecution> executions{};
  appendWorkerFault(
      executions, plan, WorkerJournalResult{}, NativeFault{.kind = NativeFaultKind::IsolationUnavailable});
  return executions;
}

[[nodiscard]] auto makeWorkerRequest(const InvocationPlan &plan, const Pair<Path, Path> &paths)
    -> WorkerRequest {
  return WorkerRequest{
      .resultPath = paths.first,
      .faultPath = paths.second,
      .identifier = plan.plannedCase.get().descriptor().identifier,
      .plannedCase = plan.plannedCaseIndex,
      .runIteration = plan.scheduledCase.runIteration,
      .runSeed = plan.runSeed,
      .timeMode = plan.options.get().timeMode,
      .traceMode = plan.options.get().traceMode,
      .captureMemory = plan.captureMemory,
  };
}

[[nodiscard]] auto launchWorkerSafely(const WorkerRequest &request, const Path &executable)
    -> isolation::WorkerOutcome {
  isolation::WorkerOutcome outcome{};
  try {
    outcome = isolation::launchWorker(isolation::WorkerLaunch{
        .executable = executable,
        .variables = workerVariables(request),
    });
  } catch (const std::exception &exception) {
    outcome.error = exception.what() != nullptr ? exception.what() : "worker launch threw an exception";
  } catch (...) {
    outcome.error = "worker launch threw a non-standard exception";
  }
  return outcome;
}

[[nodiscard]] auto readWorkerJournalSafely(const Path &resultPath, const TestDescriptor &descriptor)
    -> Result<WorkerJournalResult> {
  try {
    return readWorkerJournal(resultPath, descriptor);
  } catch (const std::exception &exception) {
    return bail(
        {exception.what() != nullptr ? exception.what() : "worker journal decoding threw an exception"});
  } catch (...) {
    return bail({"worker journal decoding threw a non-standard exception"});
  }
}

auto appendWorkerOutcome(Vec<TestExecution> &executions,
    const InvocationPlan &plan,
    const WorkerJournalResult &journal,
    const isolation::WorkerOutcome &outcome) -> void {
  if (outcome.fault and not journal.completed) {
    appendWorkerFault(executions, plan, journal, *outcome.fault);
    return;
  }

  if (not outcome.error.empty()) {
    appendWorkerProtocolFailure(executions, plan, journal, outcome.error);
    return;
  }

  if (not outcome.launched) {
    appendWorkerProtocolFailure(
        executions, plan, journal, "the process-per-case worker could not be launched");
    return;
  }

  if (journal.completed and executions.empty()) {
    appendWorkerProtocolFailure(
        executions, plan, journal, "worker completed its journal without an execution record");
    return;
  }

  if (journal.completed)
    return;

  if (outcome.exitCode == 0) {
    appendWorkerProtocolFailure(executions, plan, journal, "worker exited without completing its journal");
    return;
  }

  appendWorkerFault(
      executions, plan, journal, NativeFault{.kind = NativeFaultKind::Terminated, .code = outcome.exitCode});
}

[[nodiscard]] auto executeIsolatedCase(const InvocationPlan &plan) -> Vec<TestExecution> {
  const Option<Pair<Path, Path>> paths = workerPaths();
  if (not paths)
    return unavailableWorker(plan);
  const auto &[resultPath, faultPath] = *paths;

  const auto cleanupWorkerFiles = std::scope_exit([&] -> void { removeWorkerFiles(resultPath, faultPath); });

  const Result<Path> executable = isolation::executablePath();
  if (not executable)
    return unavailableWorker(plan);

  const WorkerRequest request = makeWorkerRequest(plan, *paths);
  const isolation::WorkerOutcome outcome = launchWorkerSafely(request, *executable);

  Result<WorkerJournalResult> journal =
      readWorkerJournalSafely(resultPath, plan.plannedCase.get().descriptor());
  if (not journal) {
    Vec<TestExecution> executions{};
    appendWorkerProtocolFailure(executions, plan, WorkerJournalResult{}, journal.error().display());
    return executions;
  }

  Vec<TestExecution> executions = std::move(journal->executions);

  appendWorkerOutcome(executions, plan, *journal, outcome);

  return executions;
}

[[nodiscard]] auto makeExecutionLanes(const Vec<PlannedCase> &plannedCases, usize repeat)
    -> Vec<UPtr<ExecutionLane>> {
  if (repeat <= 1)
    return {};

  Vec<UPtr<ExecutionLane>> executionLanes{};
  executionLanes.reserve(plannedCases.size());
  std::ranges::for_each(plannedCases, [&executionLanes](const PlannedCase &plannedCase) -> void {
    if (plannedCase.capabilities().fixtures)
      executionLanes.push_back(std::make_unique<ExecutionLane>());
    else
      executionLanes.emplace_back(nullptr);
  });
  return executionLanes;
}

[[nodiscard]] auto executeSerialCases(DispatchContext &context, const Vec<ScheduledCase> &scheduledCases)
    -> Vec<TestExecution> {
  Vec<TestExecution> executions{};
  executions.reserve(scheduledCases.size());
  bool stopped{};
  std::ranges::for_each(
      scheduledCases, [&context, &executions, &stopped](const ScheduledCase &scheduledCase) -> void {
        if (stopped)
          return;

        Vec<TestExecution> batch = executeScheduledCase(context, scheduledCase);
        stopped = context.options.get().failFast and batchFailed(batch);
        executions.append_range(std::move(batch));
      });
  return executions;
}

class ParallelCaseExecutor final {
public:
  ParallelCaseExecutor(DispatchContext &context, const Vec<ScheduledCase> &scheduledCases, usize workers)
      : context_(context)
      , scheduledCases_(scheduledCases)
      , executions_(scheduledCases.size())
      , workers_(workers) {
  }

  [[nodiscard]] auto run() -> Vec<TestExecution> {
    {
      Vec<std::jthread> threads{};
      threads.reserve(workers_);
      std::ranges::for_each(std::views::indices(workers_), [this, &threads](usize) -> void {
        threads.emplace_back(&ParallelCaseExecutor::executeNext, this);
      });
    }

    return executions_ | std::views::filter([](const Option<Vec<TestExecution>> &execution) -> bool {
      return execution.has_value();
    }) | std::views::transform([](Option<Vec<TestExecution>> &execution) -> Vec<TestExecution> {
      return std::move(*execution);
    }) | std::views::join |
           std::ranges::to<Vec<TestExecution>>();
  }

private:
  auto executeNext() -> void {
    DispatchContext &context = context_.get();
    const Vec<ScheduledCase> &scheduledCases = scheduledCases_.get();

    // Every iteration claims one unique schedule slot. The worker exits when the queue is exhausted or the
    // fail-fast flag is published; completed slots remain immutable for the final ordered flattening.
    while (true) {
      if (stopped_.load(std::memory_order_relaxed))
        return;

      const usize index = nextIndex_.fetch_add(1, std::memory_order_relaxed);
      if (index >= scheduledCases.size())
        return;

      Vec<TestExecution> batch = executeScheduledCase(context, scheduledCases[index]);
      const bool failed = batchFailed(batch);
      executions_[index].emplace(std::move(batch));

      if (context.options.get().failFast and failed)
        stopped_.store(true, std::memory_order_relaxed);
    }
  }

  Ref<DispatchContext> context_;
  Ref<const Vec<ScheduledCase>> scheduledCases_;
  Vec<Option<Vec<TestExecution>>> executions_;
  const usize workers_;
  std::atomic<usize> nextIndex_;
  std::atomic<bool> stopped_;
};

[[nodiscard]] auto executeParallelCases(DispatchContext &context,
    const Vec<ScheduledCase> &scheduledCases,
    usize workers) -> Vec<TestExecution> {
  return ParallelCaseExecutor{context, scheduledCases, workers}.run();
}

} // namespace

auto executePlannedCases(RunSession &session, const RunOptions &options) -> Vec<TestExecution> {
  if constexpr (build::tests) {
    Vec<PlannedCase> plannedCases = session.takePlannedCases();
    const u64 runSeed = options.seed ? *options.seed : randomSeed();
    const Vec<ScheduledCase> scheduledCases = scheduleCases(plannedCases.size(), options, runSeed);
    const usize workers = workerCount(options.jobs, scheduledCases.size());
    if (workers == 0)
      return {};

    Vec<UPtr<ExecutionLane>> executionLanes = makeExecutionLanes(plannedCases, options.repeat);
    DispatchContext context{
        .plannedCases = plannedCases,
        .executionLanes = executionLanes,
        .options = options,
        .runSeed = runSeed,
        .captureMemory = workers == 1,
    };

    if (workers == 1)
      return executeSerialCases(context, scheduledCases);

    return executeParallelCases(context, scheduledCases, workers);
  } else {
    return {};
  }
}

auto executeWorkerCase(RunSession &session, const WorkerRequest &request, RunOptions options) -> void {
  Vec<PlannedCase> plannedCases = session.takePlannedCases();
  WorkerJournal journal{request.resultPath};
  if (not journal.ready())
    return;

  const auto requested =
      std::ranges::find_if(plannedCases, [&request](const PlannedCase &plannedCase) -> bool {
        return plannedCase.descriptor().identifier == request.identifier;
      });
  if (requested == plannedCases.end()) {
    TestExecution failure = workerFailure(TestDescriptor{.identifier = request.identifier},
        workerProtocolDiagnostic(
            "worker could not find the requested test case", std::source_location::current()));
    journal.attemptStarted(AttemptIndex{.runIteration = request.runIteration}, false);
    journal.attemptCompleted(failure);
    journal.complete();
    return;
  }

  const usize plannedCase = static_cast<usize>(std::ranges::distance(plannedCases.begin(), requested));

  options.isolation = CrashIsolation::InProcess;
  options.jobs = 1;
  options.repeat = 1;
  options.seed = request.runSeed;
  options.timeMode = request.timeMode;
  options.traceMode = request.traceMode;

  const ScheduledCase scheduledCase{
      .plannedCase = request.plannedCase,
      .runIteration = request.runIteration,
  };

  const InvocationPlan plan{
      .plannedCase = std::cref(plannedCases[plannedCase]),
      .capabilities = plannedCases[plannedCase].capabilities(),
      .plannedCaseIndex = request.plannedCase,
      .scheduledCase = scheduledCase,
      .options = options,
      .runSeed = request.runSeed,
      .isolation = CrashIsolation::InProcess,
      .captureMemory = request.captureMemory,
      .journal = journal,
  };

  if (not isolation::installWorkerFaultHandler(request.faultPath)) {
    const TestDescriptor descriptor = plannedCases[plannedCase].descriptor();
    const NativeFault fault{
        .kind = NativeFaultKind::IsolationUnavailable,
    };
    Vec<TestExecution> failures{};
    appendWorkerFailure(
        failures, plan, WorkerJournalResult{}, workerFailure(descriptor, fault, descriptor.location));
    journal.attemptStarted(failures.front().attempt, false);
    journal.attemptCompleted(failures.front());
    journal.complete();
    return;
  }

  static_cast<void>(executeCase(plan));
  journal.complete();
}

} // namespace Nyx::Test::detail
