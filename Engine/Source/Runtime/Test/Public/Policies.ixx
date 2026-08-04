export module Nyx.Test:Policies;

import std;
import Nyx.Core;

export namespace Nyx::Test {

struct TestPolicy final {
  bool trace{};
  Option<String> expectedPanic;
  Option<std::chrono::steady_clock::duration> timeout;
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
