export module Nyx.Test:Policies;

import std;
import Nyx.Core;

export namespace Nyx::Test {

/// Chooses which test environments capture trace events for a run.
///
/// ForcedFailures captures every case but renders its trace only with a failure. ForcedAll additionally
/// renders traces for passing cases. The test-level [[= trace]] annotation is always honored.
enum class TraceMode : u8 {
  Annotations,
  ForcedFailures,
  ForcedAll,
};

struct TestPolicy final {
  bool trace{};
  Option<String> expectedPanic;
  Option<std::chrono::steady_clock::duration> timeout;
  usize repeat{1};
  usize warmup{};
  usize retry{};
};

/// Internal exception used by panic(). The runner turns it into a structured diagnostic, while shouldPanic()
/// can recognize it as an expected outcome.
class TestPanic final : public std::exception {
public:
  TestPanic(String message, std::source_location location) noexcept;

  [[nodiscard]] auto message() const noexcept -> StringView;

  [[nodiscard]] auto location() const noexcept -> std::source_location;

  [[nodiscard]] auto what() const noexcept -> const char * override;

private:
  String message_;
  std::source_location location_;
};

[[noreturn]] auto panic(StringView message, std::source_location location = std::source_location::current())
    -> void;

}
