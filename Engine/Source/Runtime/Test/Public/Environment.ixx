export module Nyx.Test:Environment;

import std;
import Nyx.Core;
import :Diagnostics;
import :Resources;

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

  /// Returns the cancellation state for the test currently being executed.
  [[nodiscard]] auto stopToken() const noexcept -> std::stop_token;

  /// Requests cooperative cancellation of the running coroutine test. The execution boundary uses this after
  /// a timeout deadline is reached.
  auto requestStop() noexcept -> void;

  [[nodiscard]] auto stopRequested() const noexcept -> bool;

  [[nodiscard]] auto resources() noexcept -> TestResources &;

  [[nodiscard]] auto profileSink() noexcept -> profiling::ProfileSink &;

  auto finalize(std::source_location location = std::source_location::current()) -> void;

  [[nodiscard]] auto resourceSnapshot() const noexcept -> ResourceSnapshot;

  [[nodiscard]] auto profileSnapshot() const noexcept -> profiling::ProfileSnapshot;

  [[nodiscard]] auto takeState() && noexcept -> TestState;

private:
  TestState state_{};
  std::stop_source stopSource_;
  TestResources resources_;
  profiling::ProfileSink profile_;
  bool traceEnabled_{};
  bool finalized_{};
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

[[nodiscard]] auto currentResources() noexcept -> Option<Ref<TestResources>>;

/// Records a user-defined trace event when the current test has [[=trace]].
/// It does not affect assertion counts and is otherwise a no-op.
auto traceEvent(StringView message, std::source_location location = std::source_location::current()) -> void;

} // namespace Nyx::Test
