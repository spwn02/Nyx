module Nyx.Test;

import :Runner;

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
    throw std::invalid_argument{"Nyx::Test RunOptions::repeat must be greater than zero"};

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
  TestExecution execution = plannedCase.execute(TestDescriptor{plannedCase.descriptor}, options.timeMode);
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
    bool captureMemory) -> Vec<TestExecution> {
  std::unique_lock<std::mutex> lock{};
  if (lane != nullptr)
    lock = std::unique_lock<std::mutex>{lane->mutex()};

  Vec<TestExecution> executions{};
  const usize samples = std::max<usize>(plannedCase.descriptor.policy.repeat, 1);
  const auto appendAttempt = [&](AttemptIndex attempt, bool warmup) -> const TestExecution & {
    TestExecution execution = executeAttempt(plannedCase,
        plannedCaseIndex,
        scheduledCase.runIteration,
        attempt,
        warmup,
        captureMemory,
        options,
        runSeed);
    executions.push_back(std::move(execution));
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

[[nodiscard]] constexpr auto workerCount(usize requested, usize workCount) -> usize {
  if (workCount == 0)
    return 0;

  if (requested == 1)
    return 1;

  if (requested == 0) {
    const usize available = std::thread::hardware_concurrency();
    return std::min(std::max<usize>(available, 1), workCount);
  }

  return std::min(requested, workCount);
}

} // namespace

auto executePlannedCases(RunSession &session, const RunOptions &options) -> Vec<TestExecution> {
  Vec<PlannedCase> plannedCases = session.takePlannedCases();
  const u64 runSeed = options.seed ? *options.seed : randomSeed();
  const Vec<ScheduledCase> scheduledCases = scheduleCases(plannedCases.size(), options, runSeed);
  const usize workers = workerCount(options.jobs, scheduledCases.size());
  const bool captureMemory = workers == 1;

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

      Vec<TestExecution> batch = executeCase(plannedCases[scheduledCase.plannedCase],
          laneFor(resourceLanes, scheduledCase.plannedCase),
          scheduledCase.plannedCase,
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
      Vec<TestExecution> batch = executeCase(plannedCases[scheduledCase.plannedCase],
          laneFor(resourceLanes, scheduledCase.plannedCase),
          scheduledCase.plannedCase,
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
}

} // namespace Nyx::Test::detail
