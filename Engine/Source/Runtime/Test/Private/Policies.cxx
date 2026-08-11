module Nyx.Test;

import :Policies;

import std;
import Nyx.Core;

namespace Nyx::Test {

TestPanic::TestPanic(String message, std::source_location location) noexcept
    : message_(std::move(message))
    , location_(location) {
}

auto TestPanic::message() const noexcept -> StringView {
  return message_;
}

auto TestPanic::location() const noexcept -> std::source_location {
  return location_;
}

auto TestPanic::what() const noexcept -> const char * {
  return message_.c_str();
}

auto panic(StringView message, std::source_location location) -> void {
  if constexpr (build::tests) {
    throw TestPanic{String{message}, location};
  } else {
    fatal("Nyx::Test::panic() is reserved for tests. Call Nyx::fatal() instead.");
  }
}

} // namespace Nyx::Test
