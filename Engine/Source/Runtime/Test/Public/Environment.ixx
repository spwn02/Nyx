export module Nyx.Test:Environment;

import std;
import Nyx.Core;
import :Diagnostics;

export namespace Nyx::Test {

struct TraceEvent final {
  String message;
  std::source_location location;
};

struct TestState final {
  usize assertions{};
  usize failedAssertions{};
  usize errors{};
  bool aborted{};
  Vec<Diagnostic> diagnostics;
  Vec<TraceEvent> traces;

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

class TestEnvironment final {
public:
  TestEnvironment() = default;

  [[nodiscard]] auto state() const noexcept -> const TestState &;

  auto recordPass() noexcept -> void;

  auto recordFailure(Diagnostic diagnostic) -> void;

  auto recordError(Diagnostic diagnostic) -> void;

  auto enableTrace() noexcept -> void;

  auto recordTrace(String message, std::source_location location = std::source_location::current()) -> void;

  auto acceptExpectedPanic(usize diagnosticIndex) noexcept -> void;

  auto abort() noexcept -> void;

  [[nodiscard]] auto takeState() && noexcept -> TestState;

private:
  TestState state_{};
  bool traceEnabled_{};
};

/// Internal control flow used by require(). The test boundary catches this type and still finalize the
/// environment normally.
struct TestAbort final {};

/// Binds an environment to the current execution thread.
///
/// Coroutine scheduling will create this binding around each resume, which makes the state task-local without
/// giving TestEnvironment global ownership.
class EnvironmentBinding final {
public:
  explicit EnvironmentBinding(TestEnvironment &environment) noexcept;
  ~EnvironmentBinding() noexcept;

  EnvironmentBinding(const EnvironmentBinding &) = delete (
      "EnvironmentBinding stores pointer to a thread-local environment.");
  auto operator=(const EnvironmentBinding &)
      -> EnvironmentBinding & = delete ("EnvironmentBinding stores pointer to a thread-local environment.");
  EnvironmentBinding(EnvironmentBinding &&) noexcept = delete (
      "EnvironmentBinding stores pointer to a thread-local environment.");
  auto operator=(EnvironmentBinding &&) noexcept
      -> EnvironmentBinding & = delete ("EnvironmentBinding stores pointer to a thread-local environment.");

private:
  TestEnvironment *previous_{};
};

[[nodiscard]] auto currentEnvironment() noexcept -> Option<Ref<TestEnvironment>>;

/// Records a user-defined trace event when the current test has [[=trace]].
/// It does not affect assertion counts and is otherwise a no-op.
auto traceEvent(StringView message, std::source_location location = std::source_location::current()) -> void;

} // namespace Nyx::Test
