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

} // namespace Tests::discovery
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::discovery>();
}
