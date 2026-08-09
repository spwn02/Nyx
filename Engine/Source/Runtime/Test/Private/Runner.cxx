module Nyx.Test;

import :Runner;

import std;
import Nyx.Core;

namespace Nyx::Test::detail {

namespace {

struct ScheduledCase final {
  usize plannedCase{};
  usize iteration{};
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

[[nodiscard]] constexpr auto deriveSeed(u64 runSeed, usize plannedCase, usize iteration) -> u64 {
  constexpr u64 streamSalt{0xD1B54A32D192ED03ULL};
  constexpr u32 iterationShift{1};
  SplitMix64 mixer{runSeed ^ streamSalt};
  const u64 caseSeed = mixer.next() ^ static_cast<u64>(plannedCase);
  const u64 iterationSeed = mixer.next() ^ static_cast<u64>(iteration);
  SplitMix64 result{caseSeed ^ (iterationSeed << iterationShift)};
  return result.next();
}

[[nodiscard]] auto scheduleCases(usize plannedCaseCount, const RunOptions &options, u64 runSeed)
    -> Vec<ScheduledCase> {
  if (options.repeat == 0)
    throw std::invalid_argument{"Nyx::Test RunOptions::repeat must be greater than zero"};

  Vec<ScheduledCase> scheduled{};
  scheduled.reserve(plannedCaseCount * options.repeat);
  std::ranges::for_each(
      std::views::indices(options.repeat), [&scheduled, plannedCaseCount, runSeed](usize iteration) -> void {
        std::ranges::for_each(std::views::indices(plannedCaseCount),
            [&scheduled, iteration, runSeed](usize plannedCase) -> void {
              scheduled.push_back(ScheduledCase{
                  .plannedCase = plannedCase,
                  .iteration = iteration,
                  .seed = deriveSeed(runSeed, plannedCase, iteration),
              });
            });
      });

  if (options.order == ExecutionOrder::Declaration or scheduled.size() < 2)
    return scheduled;

  SplitMix64 random{runSeed};
  std::ranges::for_each(std::views::iota(usize{1}, scheduled.size()) | std::views::reverse,
      [&scheduled, &random](usize index) -> void {
        const auto selected = static_cast<usize>(random.next() % (index + 1));
        std::ranges::swap(scheduled[index], scheduled[selected]);
      });
  return scheduled;
}

[[nodiscard]] constexpr auto forceTrace(TraceMode traceMode) noexcept -> bool {
  return traceMode != TraceMode::Annotations;
}

auto executeCase(PlannedCase &plannedCase,
    ResourceLane &lane,
    const ScheduledCase &scheduledCase,
    const RunOptions &options,
    u64 runSeed) -> TestExecution {
  const InvocationSettings settings{
      .seed = scheduledCase.seed,
      .iteration = scheduledCase.iteration,
      .forceTrace = forceTrace(options.traceMode),
  };
  const InvocationBinding binding{settings};
  const std::scoped_lock lock{lane.mutex()};
  TestExecution execution = plannedCase.execute(TestDescriptor{plannedCase.descriptor}, options.timeMode);
  execution.runSeed = runSeed;
  execution.traceMode = options.traceMode;
  return execution;
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
  Vec<UPtr<ResourceLane>> resourceLanes{};
  resourceLanes.reserve(plannedCases.size());
  std::ranges::for_each(plannedCases, [&resourceLanes](const PlannedCase & /*ignored*/) -> void {
    resourceLanes.push_back(std::make_unique<ResourceLane>());
  });
  const u64 runSeed = options.seed ? *options.seed : randomSeed();
  const Vec<ScheduledCase> scheduledCases = scheduleCases(plannedCases.size(), options, runSeed);
  const usize workers = workerCount(options.jobs, plannedCases.size());

  if (workers == 0)
    return {};

  if (workers == 1) {
    Vec<TestExecution> executions{};
    executions.reserve(scheduledCases.size());
    bool stopped{};
    std::ranges::for_each(scheduledCases, [&](const ScheduledCase &scheduledCase) -> void {
      if (stopped)
        return;

      TestExecution execution = executeCase(plannedCases[scheduledCase.plannedCase],
          *resourceLanes[scheduledCase.plannedCase],
          scheduledCase,
          options,
          runSeed);
      stopped = options.failFast and execution.failed();
      executions.push_back(std::move(execution));
    });

    return executions;
  }

  Vec<Option<TestExecution>> executions{scheduledCases.size()};

  std::atomic<usize> nextIndex{};
  std::atomic<bool> stopped{};
  const auto executeNext =
      [&plannedCases, &resourceLanes, &scheduledCases, &executions, &nextIndex, &stopped, &options, runSeed]
      -> void {
    while (true) {
      if (stopped.load(std::memory_order_relaxed))
        return;

      const usize index = nextIndex.fetch_add(1, std::memory_order_relaxed);
      if (index >= scheduledCases.size())
        return;

      const ScheduledCase &scheduledCase = scheduledCases[index];
      TestExecution execution = executeCase(plannedCases[scheduledCase.plannedCase],
          *resourceLanes[scheduledCase.plannedCase],
          scheduledCase,
          options,
          runSeed);
      executions[index].emplace(std::move(execution));

      if (options.failFast and executions[index]->failed())
        stopped.store(true, std::memory_order_relaxed);
    }
  };

  {
    Vec<std::jthread> threads{};
    threads.reserve(workers);
    std::ranges::for_each(std::views::indices(workers),
        [&threads, &executeNext](usize) -> void { threads.emplace_back(executeNext); });
  }

  return executions | std::views::filter([](const Option<TestExecution> &execution) -> bool {
    return execution.has_value();
  }) | std::views::transform([](Option<TestExecution> &execution) -> TestExecution {
    return std::move(*execution);
  }) | std::ranges::to<Vec<TestExecution>>();
}

} // namespace Nyx::Test::detail
