module Nyx.Test;

import :Task;

import std;
import Nyx.Core;

namespace Nyx::Test {

namespace detail {

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming)
thread_local RunLoop *currentRunLoop_{};

} // namespace

auto RunLoop::enqueue(std::coroutine_handle<> handle) -> void {
  if (not handle or handle.done())
    return;

  ready_.push_back(handle);
}

auto RunLoop::dequeue() -> Option<std::coroutine_handle<>> {
  if (ready_.empty())
    return None;

  const std::coroutine_handle<> handle = ready_.front();
  ready_.pop_front();
  return handle;
}

RunLoopBinding::RunLoopBinding(RunLoop &runLoop) noexcept
    : previous_(currentRunLoop_) {
  currentRunLoop_ = std::addressof(runLoop);
}

RunLoopBinding::~RunLoopBinding() noexcept {
  currentRunLoop_ = previous_;
}

auto currentRunLoop() noexcept -> RunLoop * {
  return currentRunLoop_;
}

auto schedule(std::coroutine_handle<> handle) -> void {
  RunLoop *const runLoop = currentRunLoop();
  if (runLoop == nullptr)
    throw std::logic_error{"Nyx::Test Task suspension requires an active test run loop."};

  runLoop->enqueue(handle);
}

} // namespace detail

auto YieldAwaiter::await_suspend(std::coroutine_handle<> handle) const -> void { // NOLINT
  detail::schedule(handle);
}

} // namespace Nyx::Test
