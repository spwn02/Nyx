module Nyx.Test;

import :Context;

import std;
import Nyx.Core;

namespace Nyx::Test {

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming)
thread_local const Context *currentContext_{};

} // namespace

ContextBinding::ContextBinding(const Context &context) noexcept
    : previous_(currentContext_) {
  currentContext_ = std::addressof(context);
}

ContextBinding::~ContextBinding() noexcept {
  currentContext_ = previous_;
}

auto currentContext() noexcept -> Option<Ref<const Context>> {
  if (currentContext_ == nullptr)
    return None;

  return std::cref(*currentContext_);
}

} // namespace Nyx::Test
