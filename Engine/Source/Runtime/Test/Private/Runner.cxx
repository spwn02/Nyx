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
  u64 seed{};
};

/// Serializes repeated invocations of one move-only planned-case closure.
///
/// Separate planed cases reatin full worker parallelism. Repeating the same case is deliberately one lane:
/// its closure may own mutable state and std::move_only_function itself is not concurrently invocable.
struct ResourceLane final {
  [[nodiscard]] auto mutex() noexcept -> std::mutex & {
    return mutex_;
  }

private:
  std::mutex mutex_;
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

auto executeAttempt(PlannedCase &plannedCase,
    usize plannedCaseIndex,
    usize runIteration,
    AttemptIndex attempt,
    bool warmup,
    bool captureMemory,
    const RunOptions &options,
    u64 runSeed) -> TestExecution {
  const InvocationSettings settings{
      .seed = deriveSeed(runSeed, plannedCaseIndex, attempt, warmup),
      .iteration = runIteration,
      .sample = attempt.sample,
      .retry = attempt.retry,
      .warmup = warmup,
      .forceTrace = forceTrace(options.traceMode),
      .captureMemory = captureMemory,
  };
  const InvocationBinding binding{settings};
  TestExecution execution{};
  try {
    execution = plannedCase.execute(TestDescriptor{plannedCase.descriptor}, options.timeMode);
  } catch (const TestAbort &) {
    execution = TestExecution{
        .descriptor = TestDescriptor{plannedCase.descriptor},
    };
    execution.state.aborted = true;
  } catch (const std::exception &exception) {
    const char *message = exception.what();
    execution = boundaryFailure(TestDescriptor{plannedCase.descriptor},
        message == nullptr ? String{"standard exception"} : String{message});
  } catch (...) {
    execution = boundaryFailure(TestDescriptor{plannedCase.descriptor}, "non-standard exception");
  }
  execution.runSeed = runSeed;
  execution.attempt = attempt;
  execution.iteration = runIteration;
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

auto executeCase(PlannedCase &plannedCase,
    ResourceLane *lane,
    usize plannedCaseIndex,
    const ScheduledCase &scheduledCase,
    const RunOptions &options,
    u64 runSeed,
    bool captureMemory,
    WorkerJournal *journal = nullptr) -> Vec<TestExecution> {
  std::unique_lock<std::mutex> lock{};
  if (lane != nullptr)
    lock = std::unique_lock<std::mutex>{lane->mutex()};

  Vec<TestExecution> executions{};
  const usize samples = std::max<usize>(plannedCase.descriptor.policy.repeat, 1);
  const auto appendAttempt = [&](AttemptIndex attempt, bool warmup) -> const TestExecution & {
    if (journal != nullptr)
      journal->attemptStarted(attempt, warmup);

    TestExecution execution = executeAttempt(plannedCase,
        plannedCaseIndex,
        scheduledCase.runIteration,
        attempt,
        warmup,
        captureMemory,
        options,
        runSeed);
    executions.push_back(std::move(execution));
    if (journal != nullptr)
      journal->attemptCompleted(executions.back());
    return executions.back();
  };

  std::ranges::for_each(std::views::indices(plannedCase.descriptor.policy.warmup),
      [&appendAttempt, &scheduledCase](usize warmupIndex) -> void {
        static_cast<void>(appendAttempt(
            AttemptIndex{
                .runIteration = scheduledCase.runIteration,
                .sample = warmupIndex,
            },
            true));
      });

  std::ranges::for_each(std::views::indices(samples), [&](usize sample) -> void {
    usize retryIndex{};
    bool retrying{true};
    while (retrying) {
      const TestExecution &execution = appendAttempt(
          AttemptIndex{
              .runIteration = scheduledCase.runIteration,
              .sample = sample,
              .retry = retryIndex,
          },
          false);
      retrying = not execution.passed() and hasTimeout(execution) and
                 retryIndex < plannedCase.descriptor.policy.retry;
      if (retrying)
        ++retryIndex;
    }
  });

  return executions;
}

[[nodiscard]] auto laneFor(Vec<UPtr<ResourceLane>> &resourceLanes, usize plannedCase) noexcept
    -> ResourceLane * {
  if (resourceLanes.empty())
    return nullptr;

  return resourceLanes[plannedCase].get();
}

[[nodiscard]] constexpr auto isolationFor(const PlannedCase &plannedCase, const RunOptions &options) noexcept
    -> CrashIsolation {
  if (plannedCase.descriptor.policy.isolated)
    return CrashIsolation::ProcessPerCase;

  return options.isolation;
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
    const ScheduledCase &scheduledCase,
    const RunOptions &options,
    u64 runSeed,
    const WorkerJournalResult &journal,
    TestExecution execution) -> void {
  if (journal.activeAttempt and not journal.activeWarmup) {
    const auto completed = std::ranges::find_if(
        executions, [&journal](const TestExecution &execution) constexpr noexcept -> bool {
          return execution.attempt == *journal.activeAttempt;
        });
    if (completed != executions.end())
      return;
  }

  execution.runSeed = runSeed;
  execution.iteration = scheduledCase.runIteration;
  execution.attempt = journal.activeAttempt.value_or(AttemptIndex{
      .runIteration = scheduledCase.runIteration,
  });
  execution.warmup = journal.activeWarmup;
  execution.seed = deriveSeed(runSeed, scheduledCase.plannedCase, execution.attempt, execution.warmup);
  execution.traceMode = options.traceMode;

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
    const PlannedCase &plannedCase,
    const ScheduledCase &scheduledCase,
    const RunOptions &options,
    u64 runSeed,
    const WorkerJournalResult &journal,
    NativeFault fault) -> void {
  appendWorkerFailure(executions,
      scheduledCase,
      options,
      runSeed,
      journal,
      workerFailure(plannedCase.descriptor, fault, plannedCase.descriptor.location));
}

auto appendWorkerProtocolFailure(Vec<TestExecution> &executions,
    const PlannedCase &plannedCase,
    const ScheduledCase &scheduledCase,
    const RunOptions &options,
    u64 runSeed,
    const WorkerJournalResult &journal,
    StringView message) -> void {
  appendWorkerFailure(executions,
      scheduledCase,
      options,
      runSeed,
      journal,
      workerFailure(
          plannedCase.descriptor, workerProtocolDiagnostic(message, plannedCase.descriptor.location)));
}

// NOLINTNEXTLINE(readability-function-size)
[[nodiscard]] auto executeIsolatedCase(PlannedCase &plannedCase,
    usize plannedCaseIndex,
    const ScheduledCase &scheduledCase,
    const RunOptions &options,
    u64 runSeed) -> Vec<TestExecution> {
  const Option<Pair<Path, Path>> paths = workerPaths();
  if (not paths) {
    Vec<TestExecution> executions{};
    appendWorkerFault(executions,
        plannedCase,
        scheduledCase,
        options,
        runSeed,
        WorkerJournalResult{},
        NativeFault{.kind = NativeFaultKind::IsolationUnavailable});
    return executions;
  }

  const Result<Path> executable = isolation::executablePath();
  if (not executable) {
    Vec<TestExecution> executions{};
    appendWorkerFault(executions,
        plannedCase,
        scheduledCase,
        options,
        runSeed,
        WorkerJournalResult{},
        NativeFault{.kind = NativeFaultKind::IsolationUnavailable});
    return executions;
  }

  const WorkerRequest request{
      .resultPath = paths->first,
      .faultPath = paths->second,
      .identifier = plannedCase.descriptor.identifier,
      .plannedCase = plannedCaseIndex,
      .runIteration = scheduledCase.runIteration,
      .runSeed = runSeed,
      .timeMode = options.timeMode,
      .traceMode = options.traceMode,
      .captureMemory = true,
  };
  isolation::WorkerOutcome outcome{};
  try {
    outcome = isolation::launchWorker(isolation::WorkerLaunch{
        .executable = *executable,
        .variables = workerVariables(request),
    });
  } catch (const std::exception &exception) {
    outcome.error = exception.what() != nullptr ? exception.what() : "worker launch threw an exception";
  } catch (...) {
    outcome.error = "worker launch threw a non-standard exception";
  }

  WorkerJournalResult journal{};
  const auto cleanupWorkerFiles =
      std::scope_exit([&] -> void { removeWorkerFiles(paths->first, paths->second); });

  try {
    journal = readWorkerJournal(paths->first, plannedCase.descriptor);
  } catch (const std::exception &exception) {
    Vec<TestExecution> executions{};
    appendWorkerProtocolFailure(executions,
        plannedCase,
        scheduledCase,
        options,
        runSeed,
        journal,
        exception.what() != nullptr ? exception.what() : "worker journal decoding threw an exception");
    return executions;
  } catch (...) {
    Vec<TestExecution> executions{};
    appendWorkerProtocolFailure(executions,
        plannedCase,
        scheduledCase,
        options,
        runSeed,
        journal,
        "worker journal decoding threw a non-standard exception");
    return executions;
  }

  Vec<TestExecution> executions = std::move(journal.executions);

  if (outcome.fault and not journal.completed) {
    appendWorkerFault(executions, plannedCase, scheduledCase, options, runSeed, journal, *outcome.fault);
  } else if (not outcome.error.empty()) {
    appendWorkerProtocolFailure(
        executions, plannedCase, scheduledCase, options, runSeed, journal, outcome.error);
  } else if (not outcome.launched) {
    appendWorkerProtocolFailure(executions,
        plannedCase,
        scheduledCase,
        options,
        runSeed,
        journal,
        "the process-per-case worker could not be launched");
  } else if (journal.completed and executions.empty()) {
    appendWorkerProtocolFailure(executions,
        plannedCase,
        scheduledCase,
        options,
        runSeed,
        journal,
        "worker completed its journal without an execution record");
  } else if (not journal.completed) {
    if (outcome.exitCode == 0) {
      appendWorkerProtocolFailure(executions,
          plannedCase,
          scheduledCase,
          options,
          runSeed,
          journal,
          "worker exited without completing its journal");
    } else {
      appendWorkerFault(executions,
          plannedCase,
          scheduledCase,
          options,
          runSeed,
          journal,
          NativeFault{.kind = NativeFaultKind::Terminated, .code = outcome.exitCode});
    }
  }

  return executions;
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity, readability-function-size)
auto executePlannedCases(RunSession &session, const RunOptions &options) -> Vec<TestExecution> {
  if constexpr (build::tests) {
    Vec<PlannedCase> plannedCases = session.takePlannedCases();
    const u64 runSeed = options.seed ? *options.seed : randomSeed();
    const Vec<ScheduledCase> scheduledCases = scheduleCases(plannedCases.size(), options, runSeed);
    const usize workers = workerCount(options.jobs, scheduledCases.size());
    const bool captureMemory = options.isolation == CrashIsolation::ProcessPerCase or workers == 1;

    if (workers == 0)
      return {};

    Vec<UPtr<ResourceLane>> resourceLanes{};
    if (options.repeat > 1) {
      resourceLanes.reserve(plannedCases.size());
      std::ranges::for_each(plannedCases, [&resourceLanes](const PlannedCase & /*ignored*/) -> void {
        resourceLanes.push_back(std::make_unique<ResourceLane>());
      });
    }
    if (workers == 1) {
      Vec<TestExecution> executions{};
      executions.reserve(scheduledCases.size());
      bool stopped{};
      std::ranges::for_each(scheduledCases, [&](const ScheduledCase &scheduledCase) -> void {
        if (stopped)
          return;

        usize plannedCaseIndex = scheduledCase.plannedCase;
        PlannedCase &plannedCase = plannedCases[plannedCaseIndex];
        Vec<TestExecution> batch =
            isolationFor(plannedCase, options) == CrashIsolation::ProcessPerCase
                ? executeIsolatedCase(plannedCase, plannedCaseIndex, scheduledCase, options, runSeed)
                : executeCase(plannedCase,
                      laneFor(resourceLanes, plannedCaseIndex),
                      plannedCaseIndex,
                      scheduledCase,
                      options,
                      runSeed,
                      captureMemory);
        stopped = options.failFast and batchFailed(batch);
        executions.append_range(std::move(batch));
      });
      return executions;
    }

    Vec<Option<Vec<TestExecution>>> executions{scheduledCases.size()};

    std::atomic<usize> nextIndex{};
    std::atomic<bool> stopped{};
    const auto executeNext = [&plannedCases,
                                 &resourceLanes,
                                 &scheduledCases,
                                 &executions,
                                 &nextIndex,
                                 &stopped,
                                 &options,
                                 &captureMemory,
                                 runSeed] -> void {
      while (true) {
        if (stopped.load(std::memory_order_relaxed))
          return;

        const usize index = nextIndex.fetch_add(1, std::memory_order_relaxed);
        if (index >= scheduledCases.size())
          return;

        const ScheduledCase &scheduledCase = scheduledCases[index];
        usize plannedCaseIndex = scheduledCase.plannedCase;
        PlannedCase &plannedCase = plannedCases[plannedCaseIndex];
        Vec<TestExecution> batch =
            isolationFor(plannedCase, options) == CrashIsolation::ProcessPerCase
                ? executeIsolatedCase(plannedCase, plannedCaseIndex, scheduledCase, options, runSeed)
                : executeCase(plannedCase,
                      laneFor(resourceLanes, plannedCaseIndex),
                      plannedCaseIndex,
                      scheduledCase,
                      options,
                      runSeed,
                      captureMemory);
        const bool failed = batchFailed(batch);
        executions[index].emplace(std::move(batch));

        if (options.failFast and failed)
          stopped.store(true, std::memory_order_relaxed);
      }
    };

    {
      Vec<std::jthread> threads{};
      threads.reserve(workers);
      std::ranges::for_each(std::views::indices(workers),
          [&threads, &executeNext](usize) -> void { threads.emplace_back(executeNext); });
    }

    return executions | std::views::filter([](const Option<Vec<TestExecution>> &execution) -> bool {
      return execution.has_value();
    }) | std::views::transform([](Option<Vec<TestExecution>> &execution) -> Vec<TestExecution> {
      return std::move(*execution);
    }) | std::views::join |
           std::ranges::to<Vec<TestExecution>>();
  } else {
    return {};
  }
}

auto executeWorkerCase(RunSession &session, const WorkerRequest &request, RunOptions options) -> void {
  Vec<PlannedCase> plannedCases = session.takePlannedCases();
  const auto requested =
      std::ranges::find_if(plannedCases, [&request](const PlannedCase &plannedCase) -> bool {
        return plannedCase.descriptor.identifier == request.identifier;
      });
  if (requested == plannedCases.end())
    return;

  const usize plannedCase = static_cast<usize>(std::ranges::distance(plannedCases.begin(), requested));

  options.isolation = CrashIsolation::InProcess;
  options.jobs = 1;
  options.repeat = 1;
  options.seed = request.runSeed;
  options.timeMode = request.timeMode;
  options.traceMode = request.traceMode;

  WorkerJournal journal{request.resultPath};
  if (not journal.ready())
    return;

  const ScheduledCase scheduledCase{
      .plannedCase = request.plannedCase,
      .runIteration = request.runIteration,
  };

  if (not isolation::installWorkerFaultHandler(request.faultPath)) {
    const TestDescriptor descriptor = plannedCases[plannedCase].descriptor;
    const NativeFault fault{
        .kind = NativeFaultKind::IsolationUnavailable,
    };
    Vec<TestExecution> failures{};
    appendWorkerFailure(failures,
        scheduledCase,
        options,
        request.runSeed,
        WorkerJournalResult{},
        workerFailure(descriptor, fault, descriptor.location));
    journal.attemptStarted(failures.front().attempt, false);
    journal.attemptCompleted(failures.front());
    journal.complete();
    return;
  }

  static_cast<void>(executeCase(plannedCases[plannedCase],
      nullptr,
      request.plannedCase,
      scheduledCase,
      options,
      request.runSeed,
      request.captureMemory,
      std::addressof(journal)));
  journal.complete();
}

} // namespace Nyx::Test::detail
