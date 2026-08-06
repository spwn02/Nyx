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

RunLoop::RunLoop(std::stop_token stopToken) noexcept
    : stopToken_(std::move(stopToken)) {
}

auto RunLoop::enqueue(std::coroutine_handle<> handle) -> void {
  if (not handle or handle.done())
    return;

  ready_.push_back(handle);
}

auto RunLoop::scheduleAt(std::coroutine_handle<> handle, TimePoint wakeTime) -> void {
  if (not handle or handle.done())
    return;

  timers_.push(Timer{
      .wakeTime = wakeTime,
      .handle = handle,
  });
}

auto RunLoop::promoteDueTimers() -> void {
  const TimePoint now = Clock::now();
  while (not timers_.empty() and timers_.top().wakeTime <= now) {
    const Timer timer = timers_.top();
    timers_.pop();
    enqueue(timer.handle);
  }
}

auto RunLoop::nextTimerWake() const -> Option<TimePoint> {
  if (timers_.empty())
    return None;

  return timers_.top().wakeTime;
}

auto RunLoop::dequeue() -> Option<std::coroutine_handle<>> {
  promoteDueTimers();

  if (ready_.empty())
    return None;

  const std::coroutine_handle<> handle = ready_.front();
  ready_.pop_front();
  return handle;
}

auto RunLoop::waitForWork(Option<TimePoint> externalWake) -> WaitResult {
  promoteDueTimers();
  if (not ready_.empty())
    return WaitResult::TimerReady;

  const Option<TimePoint> timerWake = nextTimerWake();
  if (not timerWake and not externalWake)
    return WaitResult::NoWork;

  const bool externalFirst = externalWake and (not timerWake or *externalWake <= *timerWake);
  const TimePoint wakeTime = externalFirst ? *externalWake : *timerWake;
  std::this_thread::sleep_until(wakeTime);
  promoteDueTimers();

  if (externalFirst and Clock::now() >= *externalWake)
    return WaitResult::ExternalWake;

  return WaitResult::TimerReady;
}

auto RunLoop::wakeAllTimers() -> void {
  while (not timers_.empty()) {
    const Timer timer = timers_.top();
    timers_.pop();
    enqueue(timer.handle);
  }
}

auto RunLoop::stopRequested() const noexcept -> bool {
  return stopToken_.stop_requested();
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

auto scheduleAfter(std::coroutine_handle<> handle, RunLoop::Clock::duration duration) -> void {
  RunLoop *const runLoop = currentRunLoop();
  if (runLoop == nullptr)
    throw std::logic_error{"Nyx::Test Task suspension requires an active test run loop."};

  runLoop->scheduleAt(handle, RunLoop::Clock::now() + duration);
}

[[nodiscard]]
auto stopRequested() noexcept -> bool {
  const RunLoop *const runLoop = currentRunLoop();
  return runLoop != nullptr and runLoop->stopRequested();
}

} // namespace detail

auto YieldAwaiter::await_suspend(std::coroutine_handle<> handle) const -> void { // NOLINT
  if (detail::stopRequested())
    throw detail::TaskCancelled{};

  detail::schedule(handle);
}

auto SleepAwaiter::await_suspend(std::coroutine_handle<> handle) const -> void {
  if (detail::stopRequested())
    throw detail::TaskCancelled{};

  detail::scheduleAfter(handle, duration_);
}

} // namespace Nyx::Test
