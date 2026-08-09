import std;

import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

namespace Tests::stress {

namespace ParallelSubjects {

inline constexpr usize caseCount{4};

struct CaseState final {
  std::atomic<usize> active;
  std::atomic<usize> overlaps;
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> invocations{};
inline std::atomic<usize> cleanups{};
inline std::array<CaseState, caseCount> states{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

auto reset() -> void {
  invocations.store(0);
  cleanups.store(0);
  std::ranges::for_each(states, [](CaseState &state) -> void {
    state.active.store(0);
    state.overlaps.store(0);
  });
}

auto exercise(usize index) -> Task<void> {
  invocations.fetch_add(1, std::memory_order_relaxed);
  const usize previous = states.at(index).active.fetch_add(1, std::memory_order_relaxed);

  if (previous != 0)
    states.at(index).overlaps.fetch_add(1, std::memory_order_relaxed);

  const auto cleanup = std::scope_exit([] -> void { cleanups.fetch_add(1, std::memory_order_relaxed); });

  const auto release =
      std::scope_exit([index] -> void { states.at(index).active.fetch_sub(1, std::memory_order_relaxed); });

  co_await yield();
  co_await yield();
}

[[ = test, = group("framework"), = tag("stress", "parallel") ]] auto alpha() -> Task<void> {
  co_await exercise(0);
}

[[ = test, = group("framework"), = tag("stress", "parallel") ]] auto beta() -> Task<void> {
  co_await exercise(1);
}

[[ = test, = group("framework"), = tag("stress", "parallel") ]] auto gamma() -> Task<void> {
  co_await exercise(2);
}

[[ = test, = group("framework"), = tag("stress", "parallel") ]] auto delta() -> Task<void> {
  co_await exercise(3);
}

} // namespace ParallelSubjects

namespace CancellationSubjects {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> cleanups{};

auto reset() -> void {
  cleanups.store(0);
}

auto awaitBeyondTimeout() -> Task<void> {
  const auto cleanup = std::scope_exit([] -> void { cleanups.fetch_add(1, std::memory_order_relaxed); });

  co_await sleepFor(std::chrono::hours{1});
}

[[
  = test,
  = group("framework"),
  = tag("stress", "cancellation"),
  = timeout(std::chrono::milliseconds{1})
]] auto alpha() -> Task<void> {
  co_await awaitBeyondTimeout();
}

[[
  = test,
  = group("framework"),
  = tag("stress", "cancellation"),
  = timeout(std::chrono::milliseconds{1})
]] auto beta() -> Task<void> {
  co_await awaitBeyondTimeout();
}

[[
  = test,
  = group("framework"),
  = tag("stress", "cancellation"),
  = timeout(std::chrono::milliseconds{1})
]] auto gamma() -> Task<void> {
  co_await awaitBeyondTimeout();
}

[[
  = test,
  = group("framework"),
  = tag("stress", "cancellation"),
  = timeout(std::chrono::milliseconds{1})
]] auto delta() -> Task<void> {
  co_await awaitBeyondTimeout();
}

} // namespace CancellationSubjects

[[ = test, = group("framework"), = tag("stress") ]] auto preservesRepeatedAsyncLifecycle() -> void {
  constexpr usize subjectCount{ParallelSubjects::caseCount};
  constexpr usize repeatCount{3};
  constexpr usize expectedExecutions{subjectCount * repeatCount};

  ParallelSubjects::reset();

  const Vec<TestExecution> executions = runAll<^^ParallelSubjects>(RunOptions{
      .jobs = subjectCount,
      .timeMode = TimeMode::Virtual,
      .repeat = repeatCount,
      .seed = 0x51A7,
  });

  require(executions.size() == expectedExecutions);
  require(std::ranges::all_of(
      executions, [](const TestExecution &execution) -> bool { return execution.passed(); }));

  check(ParallelSubjects::invocations.load() == expectedExecutions);
  check(ParallelSubjects::cleanups.load() == expectedExecutions);
  check(std::ranges::all_of(ParallelSubjects::states,
      [](const ParallelSubjects::CaseState &state) -> bool { return state.overlaps.load() == 0; }));
}

[[ = test, = group("framework"), = tag("stress") ]] auto cleansEveryParallelTimeoutExactlyOnce() -> void {
  constexpr usize subjectCount{4};
  constexpr usize repeatCount{3};
  constexpr usize expectedExecutions{subjectCount * repeatCount};

  CancellationSubjects::reset();

  const Vec<TestExecution> executions = runAll<^^CancellationSubjects>(RunOptions{
      .jobs = subjectCount,
      .timeMode = TimeMode::Virtual,
      .repeat = repeatCount,
      .seed = 0xCA11CE,
  });

  require(executions.size() == expectedExecutions);
  require(std::ranges::all_of(
      executions, [](const TestExecution &execution) -> bool { return execution.failed(); }));

  check(CancellationSubjects::cleanups.load() == expectedExecutions);
  check(std::ranges::all_of(executions, [](const TestExecution &execution) -> bool {
    return execution.state.errors == 1 and execution.duration == std::chrono::milliseconds{1};
  }));
}

} // namespace Tests::stress

consteval {
  discover<^^Tests::stress>();
}
