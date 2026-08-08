import std;
import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::discovery {

namespace PassingTests {

constexpr auto fibonacci(u32 input) -> u32 {
  switch (input) {
    case 0: return 0;
    case 1: return 1;
    default: return fibonacci(input - 2) + fibonacci(input - 1);
  }
}

[[ = test, = Case{0, 0}, = Case{1, 1}, = Case{5, 5} ]] constexpr auto fibonacciCases(u32 input, u32 expected)
    -> bool {
  return fibonacci(input) == expected;
}

[[= test]] auto voidCase() -> void {
  require(true);
}

auto ignored() -> bool {
  return false;
}

} // namespace PassingTests

namespace ParallelTests {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> active{};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> peak{};

auto reset() -> void {
  active.store(0, std::memory_order_relaxed);
  peak.store(0, std::memory_order_relaxed);
}

auto observePeak(usize value) -> void {
  usize observed = peak.load(std::memory_order_relaxed);
  while (observed < value and not peak.compare_exchange_weak(observed, value, std::memory_order_relaxed)) {
    // pass
  }
}

[[ = test, = Case{1}, = Case{2}, = Case{3}, = Case{4} ]] auto runsConcurrently(u32 /*ignored*/)
    -> Task<void> {
  const usize current = active.fetch_add(1, std::memory_order_relaxed) + 1;
  observePeak(current);
  const auto cleanup = std::scope_exit([] -> void { active.fetch_sub(1, std::memory_order_relaxed); });

  co_await yield();
  std::this_thread::sleep_for(std::chrono::milliseconds{10});
  require(current <= 2_exp);
}

} // namespace ParallelTests

namespace ParallelFixtureTests {

inline constexpr u32 expectedSharedValue{69};
inline constexpr auto oneHour = std::chrono::hours{1};

struct SharedFixture final {
  u32 value{};
};

struct CaseFixture final {
  usize instance{};
  u32 sharedValue{};
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> sharedCreations{};
inline std::atomic<usize> caseCreations{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

auto reset() -> void {
  sharedCreations.store(0, std::memory_order_relaxed);
  caseCreations.store(0, std::memory_order_relaxed);
}

[[ = fixture, = once ]] auto sharedFixture() -> SharedFixture {
  sharedCreations.fetch_add(1, std::memory_order_relaxed);
  std::this_thread::sleep_for(std::chrono::milliseconds{10});
  return SharedFixture{
      .value = expectedSharedValue,
  };
}

[[= fixture]] auto caseFixture(const SharedFixture &shared) -> CaseFixture {
  return CaseFixture{
      .instance = caseCreations.fetch_add(1, std::memory_order_relaxed) + 1,
      .sharedValue = shared.value,
  };
}

[[ = test,
  = Case{1},
  = Case{2},
  = Case{3},
  = Case{4},
  = arg<"expectedCase">(fromCase) ]] auto resolvesFixtureScopesInParallel(u32 expectedCase,
    const SharedFixture &shared,                         // NOLINT
    const CaseFixture &caseFixtureValue) -> Task<void> { // NOLINT
  co_await sleepFor(oneHour);

  const Option<Ref<const Context>> context = currentContext();
  require(context);
  require(shared.value == expectedSharedValue);
  require(caseFixtureValue.sharedValue == expectedSharedValue);
  require(caseFixtureValue.instance != 0_exp);
  check(context->get().testCase + 1 == expectedCase);
}

} // namespace ParallelFixtureTests

namespace FailingTests {

[[ = test, = Case{2, 2}, = Case{3, 2} ]] constexpr auto identityCases(u32 input, u32 expected) -> bool {
  return input == expected;
}

} // namespace FailingTests

[[= test]] auto discoversEachDeclarativeCase() -> void {
  constexpr usize expectedCases{4};
  const Vec<TestDescriptor> descriptors = describe<^^PassingTests>();

  require(eq(descriptors.size(), expectedCases));
  check(descriptors.front().identifier == "fibonacciCases(0, 0)"_exp);
  check(descriptors[1].identifier == "fibonacciCases(1, 1)"_exp);
  check(descriptors[2].identifier == "fibonacciCases(5, 5)"_exp);
  check(descriptors.back().identifier == "voidCase"_exp);
}

[[= test]] auto dispatchesParameterisedTests() -> void {
  constexpr usize expectedCases{4};
  constexpr usize expectedAssertions{4};
  const Vec<TestExecution> executions = runAll<^^PassingTests>();
  const TestSummary summary = Reporter::summarize(executions);

  check(executions.size() == expectedCases);
  check(summary.testCount == expectedCases);
  check(summary.passedCount == expectedCases);
  check(summary.assertionCount == expectedAssertions);
  check(summary.passed());
}

[[= test]] auto preservesCaseIdentityForFailures() -> void {
  constexpr usize expectedCases{2};
  constexpr usize expectedPassed{1};
  constexpr usize expectedFailed{1};
  const Vec<TestExecution> executions = runAll<^^FailingTests>();
  const TestSummary summary = Reporter::summarize(executions);

  require(executions.size() == expectedCases);
  require(summary.passedCount == expectedPassed);
  require(summary.failedCount == expectedFailed);
  require(executions.back().failed());
  check(executions.back().descriptor.identifier == "identityCases(3, 2)"_exp);
  check(executions.back().state.diagnostics.front().description() == "test returned false"_exp);
}

[[= test]] auto dispatchesIndependentCasesInParallel() -> void {
  constexpr usize workerCount{2};
  constexpr usize expectedCases{4};
  ParallelTests::reset();
  const Vec<TestExecution> executions = runAll<^^ParallelTests>(RunOptions{
      .jobs = workerCount,
  });
  const TestSummary summary = Reporter::summarize(executions);

  require(executions.size() == expectedCases);
  require(summary.passedCount == expectedCases);
  check(ParallelTests::peak.load(std::memory_order_relaxed) >= workerCount);
  check(executions.front().descriptor.identifier == "runsConcurrently(1)");
  check(executions.back().descriptor.identifier == "runsConcurrently(4)");
}

[[= test]] auto preservesFixtureScopesAndVirtualTimeAcrossParallelCases() -> void {
  constexpr usize workerCount{4};
  constexpr usize expectedCases{4};
  const auto passed = [](const TestExecution &execution) -> bool { return execution.passed(); };

  ParallelFixtureTests::reset();
  const Vec<TestExecution> executions = runAll<^^ParallelFixtureTests>(RunOptions{
      .jobs = workerCount,
      .timeMode = TimeMode::Virtual,
  });

  require(executions.size() == expectedCases);
  require(std::ranges::all_of(executions, passed));
  require(ParallelFixtureTests::sharedCreations.load(std::memory_order_relaxed) == 1_exp);
  require(ParallelFixtureTests::caseCreations.load(std::memory_order_relaxed) == expectedCases);
  require(std::ranges::all_of(executions, [](const TestExecution &execution) -> bool {
    return execution.duration == ParallelFixtureTests::oneHour;
  }));
  check(executions.front().descriptor.identifier == "resolvesFixtureScopesInParallel(1)"_exp);
  check(executions.back().descriptor.identifier == "resolvesFixtureScopesInParallel(4)"_exp);
}

} // namespace Tests::discovery
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::discovery>();
}
