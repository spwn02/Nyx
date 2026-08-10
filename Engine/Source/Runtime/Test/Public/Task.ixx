export module Nyx.Test:Task;

import std;
import Nyx.Core;

// NOLINTBEGIN(readability-identifier-naming)
export namespace Nyx::Test {

template <class Value = void>
class Task;

/// Selects the source of time used by one Nyx::Test task run loop.
///
/// Virtual time advances only when the loop reaches its next timer or external deadline. It therefore makes
/// Nyx::Test awaitables deterministic without changing direct OS wait semantics.
enum class[[= debug::derive]] TimeMode : u8 {
  Real,
  Virtual,
};

namespace detail {

/// Internal control flow used when a run loop has requested cancellation.
/// The test boundary turns this into the timeout diagnostic after cleanup.
class TaskCancelled final {};

/// Describes a broken asynchronous lifecycle observed by the test scheduduler.
enum class[[= debug::derive]] TaskLifecycleFailure : u8 {
  Stranded,
  PendingWork,
};

/// Internal control flow used when an asynchronous test can no longer make safe progress.
class TaskLifecycleError final {
public:
  constexpr explicit TaskLifecycleError(TaskLifecycleFailure failure, usize pendingWork = 0) noexcept
      : failure_(failure)
      , pendingWork_(pendingWork) {
  }

  [[nodiscard]] constexpr auto failure() const noexcept -> TaskLifecycleFailure {
    return failure_;
  }

  [[nodiscard]] constexpr auto pendingWork() const noexcept -> usize {
    return pendingWork_;
  }

private:
  TaskLifecycleFailure failure_{};
  usize pendingWork_{};
};

class RunLoop final {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  enum class[[= debug::derive]] WaitResult : u8 {
    NoWork,
    TimerReady,
    ExternalWake,
  };

  explicit RunLoop(TimeMode timeMode = TimeMode::Real, std::stop_token stopToken = {}) noexcept;

  [[nodiscard]] auto now() const noexcept -> TimePoint;

  [[nodiscard]] auto elapsed() const noexcept -> Clock::duration;

  auto enqueue(std::coroutine_handle<> handle) -> void;

  auto scheduleAt(std::coroutine_handle<> handle, TimePoint wakeTime) -> void;

  [[nodiscard]] auto dequeue() -> Option<std::coroutine_handle<>>;

  /// Waits until a timer is ready or an external deadline is reached.
  [[nodiscard]] auto waitForWork(Option<TimePoint> externalWake) -> WaitResult;

  /// Makes every locally sleeping coroutine eligible to observe cancellation.
  auto wakeAllTimers() -> void;

  /// Discards non-owning scheduler references before their coroutine owners are destroyed.
  auto discardPending() noexcept -> void;

  /// Counts live coroutine frames with an outstanding scheduler ticket.
  [[nodiscard]] auto pendingWorkCount() const noexcept -> usize;

  [[nodiscard]] auto stopRequested() const noexcept -> bool;

  [[nodiscard]] auto timeoutTriggered() const noexcept -> bool;

private:
  struct Timer final {
    TimePoint wakeTime;
    usize sequence{};
    std::coroutine_handle<> handle;
  };

  struct TimerCompare final {
    [[nodiscard]] auto operator()(const Timer &left, const Timer &right) const noexcept -> bool {
      if (left.wakeTime != right.wakeTime)
        return left.wakeTime > right.wakeTime;

      return left.sequence > right.sequence;
    }
  };

  auto advanceTo(TimePoint time) noexcept -> void;

  auto promoteDueTimers() -> void;

  [[nodiscard]] auto nextTimerWake() const -> Option<TimePoint>;

  std::deque<std::coroutine_handle<>> ready_;
  std::priority_queue<Timer, Vec<Timer>, TimerCompare> timers_;
  /// A coroutine may own at most one ready or timer ticket at a time.
  FlatSet<void *> scheduled_;
  TimeMode timeMode_{};
  TimePoint startedAt_;
  TimePoint currentTime_;
  usize nextTimerSequence_{};
  std::stop_token stopToken_;
  bool timeoutTriggered_{};
};

/// Dynamically binds the deterministic coroutine queue while a task resumes. It is analogous to
/// EnvironmentBinding, but owns scheduling state rather than test state.
class RunLoopBinding final {
public:
  explicit RunLoopBinding(RunLoop &runLoop) noexcept;
  ~RunLoopBinding() noexcept;

  RunLoopBinding(const RunLoopBinding &) = delete (
      "RunLoopBinding owns a pointer to a previous thread-local RunLoop.");
  auto operator=(const RunLoopBinding &)
      -> RunLoopBinding & = delete ("RunLoopBinding owns a pointer to a previous thread-local RunLoop.");
  RunLoopBinding(RunLoopBinding &&) noexcept = delete (
      "RunLoopBinding owns a pointer to a previous thread-local RunLoop.");
  auto operator=(RunLoopBinding &&) noexcept
      -> RunLoopBinding & = delete ("RunLoopBinding owns a pointer to a previous thread-local RunLoop.");

private:
  RunLoop *previous_{};
};

[[nodiscard]] auto currentRunLoop() noexcept -> RunLoop *;

auto schedule(std::coroutine_handle<> handle) -> void;

auto scheduleAfter(std::coroutine_handle<> handle, RunLoop::Clock::duration duration) -> void;

[[nodiscard]] auto stopRequested() noexcept -> bool;

template <class Value, class Resume>
auto drive(Task<Value> &task, Resume &&resume) -> void;

template <class Value, class Resume, class BeforeResume>
auto drive(Task<Value> &task, Resume &&resume, BeforeResume &&beforeResume, std::stop_token stopToken = {})
    -> void;

template <class Value, class Resume, class BeforeResume, class NextWakeUp>
auto drive(Task<Value> &task,
    RunLoop &runLoop,
    Resume &&resume,
    BeforeResume &&beforeResume,
    NextWakeUp &&nextWakeUp) -> void;

template <class Value, class Resume, class BeforeResume, class NextWakeUp>
auto drive(Task<Value> &task,
    Resume &&resume,
    BeforeResume &&beforeResume,
    NextWakeUp &&nextWakeUp,
    std::stop_token stopToken = {}) -> void;

} // namespace detail

/// Cooperative suspension point for a Nyx::Test Task.
///
/// The active deterministic run loop re-enqueues the current coroutine. A future executor can replace the
/// queue without changing Task<T> call sites. After a timeout requests cancellation, a later yield() stops a
/// coroutine that did not finish during its cancellation-aware resume.
struct YieldAwaiter final {
  [[nodiscard]] constexpr auto await_ready() const noexcept -> bool { // NOLINT
    return false;
  }

  auto await_suspend(std::coroutine_handle<> handle) const -> void;

  constexpr auto await_resume() const noexcept -> void {
  }
};

[[nodiscard]] constexpr auto yield() noexcept -> YieldAwaiter {
  return {};
}

/// Cooperative timer awaiter for Nyx::Test Task.
///
/// It suspends only the current coroutine. The local test run loop waits for its deadline, and timeout
/// cancellation resumes it early so cleanup can observe Context::stopToken.
class SleepAwaiter final {
public:
  constexpr explicit SleepAwaiter(std::chrono::steady_clock::duration duration) noexcept
      : duration_(duration) {
  }

  [[nodiscard]] constexpr auto await_ready() const noexcept -> bool {
    return duration_ <= std::chrono::steady_clock::duration::zero();
  }

  auto await_suspend(std::coroutine_handle<> handle) const -> void;

  constexpr auto await_resume() const noexcept -> void {
  }

private:
  std::chrono::steady_clock::duration duration_;
};

template <class Rep, class Period>
[[nodiscard]] constexpr auto sleepFor(std::chrono::duration<Rep, Period> duration) noexcept -> SleepAwaiter {
  return SleepAwaiter{std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration)};
}

template <class Value>
class [[nodiscard]] Task final {
  static_assert(not std::is_reference_v<Value>, "Nyx::Test Task<T> cannot store a reference.");

public:
  class promise_type;
  using Handle = std::coroutine_handle<promise_type>;

  class promise_type final {
  public:
    struct FinalAwaiter final {
      [[nodiscard]] constexpr auto await_ready() const noexcept -> bool {
        return false;
      }

      [[nodiscard]] auto await_suspend(Handle handle) const noexcept -> std::coroutine_handle<> {
        const std::coroutine_handle<> continuation = handle.promise().continuation_;
        return continuation ? continuation : std::noop_coroutine();
      }

      constexpr auto await_resume() const noexcept -> void {
      }
    };

    [[nodiscard]] auto get_return_object() noexcept -> Task {
      return Task{Handle::from_promise(*this)};
    }

    [[nodiscard]] static constexpr auto initial_suspend() noexcept -> std::suspend_always {
      return {};
    }

    [[nodiscard]] static constexpr auto final_suspend() noexcept -> FinalAwaiter {
      return {};
    }

    template <class Return>
      requires std::constructible_from<Value, Return>
    auto return_value(Return &&value) -> void {
      value_.emplace(std::forward<Return>(value));
    }

    auto unhandled_exception() noexcept -> void {
      exception_ = std::current_exception();
    }

    [[nodiscard]] auto takeResult() -> Value {
      if (resultTaken_)
        throw std::logic_error{"Nyx::Test Task<T> result may be read only once."};

      resultTaken_ = true;

      if (exception_)
        std::rethrow_exception(exception_);

      if (not value_)
        throw std::logic_error{"Nyx::Test Task<T> completed without a value."};

      return std::move(*value_);
    }

    [[nodiscard]] auto hasContinuation() const noexcept -> bool {
      return static_cast<bool>(continuation_);
    }

    auto setContinuation(std::coroutine_handle<> continuation) noexcept -> void {
      continuation_ = continuation;
    }

  private:
    std::exception_ptr exception_;
    std::coroutine_handle<> continuation_;
    Option<Value> value_{};
    bool resultTaken_{};
  };

  class Awaiter final {
  public:
    Awaiter(Handle handle, bool ownsHandle) noexcept
        : handle_(handle)
        , ownsHandle_(ownsHandle) {
    }

    ~Awaiter() noexcept {
      if (ownsHandle_ and handle_)
        handle_.destroy();
    }

    Awaiter(const Awaiter &) = delete ("Awaiter may own value, which is destroyed in its destructor.");
    auto operator=(const Awaiter &)
        -> Awaiter & = delete ("Awaiter may own value, which is destroyed in its destructor.");

    Awaiter(Awaiter &&other) noexcept
        : handle_(std::exchange(other.handle_, {}))
        , ownsHandle_(std::exchange(other.ownsHandle_, false)) {
    }

    auto operator=(Awaiter &&other) noexcept -> Awaiter & {
      if (this == std::addressof(other))
        return *this;

      if (ownsHandle_ and handle_)
        handle_.destroy();

      handle_ = std::exchange(other.handle_, {});
      ownsHandle_ = std::exchange(other.ownsHandle_, false);
      return *this;
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool {
      return handle_ and handle_.done();
    }

    auto await_suspend(std::coroutine_handle<> continuation) -> void {
      if (not handle_)
        throw std::logic_error{"Nyx::Test cannot await an empty Task<T>."};

      if (handle_.promise().hasContinuation())
        throw std::logic_error{"Nyx::Test Task<T> may be awaited only once."};

      handle_.promise().setContinuation(continuation);
      detail::schedule(handle_);
    }

    [[nodiscard]] auto await_resume() -> Value {
      return handle_.promise().takeResult();
    }

  private:
    Handle handle_{};
    bool ownsHandle_{};
  };

  Task() = default;

  ~Task() noexcept {
    reset();
  }

  Task(const Task &) = delete ("Task<T> owns coroutine handle");
  auto operator=(const Task &) -> Task & = delete ("Task<T> owns coroutine handle");

  Task(Task &&other) noexcept
      : handle_(std::exchange(other.handle_, {})) {
  }

  auto operator=(Task &&other) noexcept -> Task & {
    if (this == std::addressof(other))
      return *this;

    reset();
    handle_ = std::exchange(other.handle_, {});
    return *this;
  }

  [[nodiscard]] auto valid() const noexcept -> bool {
    return static_cast<bool>(handle_);
  }

  [[nodiscard]] auto done() const noexcept -> bool {
    return not handle_ or handle_.done();
  }

  [[nodiscard]] auto takeResult() && -> Value {
    if (not handle_ or not handle_.done())
      throw std::logic_error{"Nyx::Test cannot read an incomplete Task<T>."};

    return handle_.promise().takeResult();
  }

  [[nodiscard]] auto operator co_await() & noexcept -> Awaiter {
    return Awaiter{handle_, false};
  }

  [[nodiscard]] auto operator co_await() && noexcept -> Awaiter {
    return Awaiter{std::exchange(handle_, {}), true};
  }

private:
  template <class OtherValue, class Resume, class BeforeResume, class NextWakeUp>
  friend auto detail::drive(Task<OtherValue> &task,
      detail::RunLoop &runLoop,
      Resume &&resume,
      BeforeResume &&beforeResume,
      NextWakeUp &&nextWakeUp) -> void;

  explicit Task(Handle handle) noexcept
      : handle_(handle) {
  }

  auto reset() noexcept -> void {
    if (handle_)
      handle_.destroy();

    handle_ = {};
  }

  Handle handle_{};
};

template <>
class [[nodiscard]] Task<void> final {
public:
  class promise_type;
  using Handle = std::coroutine_handle<promise_type>;

  class promise_type final {
  public:
    struct FinalAwaiter final {
      [[nodiscard]] constexpr auto await_ready() const noexcept -> bool { // NOLINT
        return false;
      }

      [[nodiscard]] auto await_suspend(Handle handle) const noexcept -> std::coroutine_handle<> { // NOLINT
        const std::coroutine_handle<> continuation = handle.promise().continuation_;
        return continuation ? continuation : std::noop_coroutine();
      }

      constexpr auto await_resume() const noexcept -> void {
      }
    };

    [[nodiscard]] auto get_return_object() noexcept -> Task {
      return Task{Handle::from_promise(*this)};
    }

    [[nodiscard]] static constexpr auto initial_suspend() noexcept -> std::suspend_always {
      return {};
    }

    [[nodiscard]] static constexpr auto final_suspend() noexcept -> FinalAwaiter {
      return {};
    }

    auto return_void() -> void {
    }

    auto unhandled_exception() noexcept -> void {
      exception_ = std::current_exception();
    }

    auto takeResult() -> void {
      if (resultTaken_)
        throw std::logic_error{"Nyx::Test Task<T> result may be read only once."};

      resultTaken_ = true;

      if (exception_)
        std::rethrow_exception(exception_);
    }

    [[nodiscard]] auto hasContinuation() const noexcept -> bool {
      return static_cast<bool>(continuation_);
    }

    auto setContinuation(std::coroutine_handle<> continuation) noexcept -> void {
      continuation_ = continuation;
    }

  private:
    std::exception_ptr exception_;
    std::coroutine_handle<> continuation_;
    bool resultTaken_{};
  };

  class Awaiter final {
  public:
    Awaiter(Handle handle, bool ownsHandle) noexcept
        : handle_(handle)
        , ownsHandle_(ownsHandle) {
    }

    ~Awaiter() noexcept {
      if (ownsHandle_ and handle_)
        handle_.destroy();
    }

    Awaiter(const Awaiter &) = delete ("Awaiter may own value, which is destroyed in its destructor.");
    auto operator=(const Awaiter &)
        -> Awaiter & = delete ("Awaiter may own value, which is destroyed in its destructor.");

    Awaiter(Awaiter &&other) noexcept
        : handle_(std::exchange(other.handle_, {}))
        , ownsHandle_(std::exchange(other.ownsHandle_, false)) {
    }

    auto operator=(Awaiter &&other) noexcept -> Awaiter & {
      if (this == std::addressof(other))
        return *this;

      if (ownsHandle_ and handle_)
        handle_.destroy();

      handle_ = std::exchange(other.handle_, {});
      ownsHandle_ = std::exchange(other.ownsHandle_, false);
      return *this;
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool {
      return handle_ and handle_.done();
    }

    auto await_suspend(std::coroutine_handle<> continuation) -> void {
      if (not handle_)
        throw std::logic_error{"Nyx::Test cannot await an empty Task<T>."};

      if (handle_.promise().hasContinuation())
        throw std::logic_error{"Nyx::Test Task<T> may be awaited only once."};

      handle_.promise().setContinuation(continuation);
      detail::schedule(handle_);
    }

    auto await_resume() -> void {
      handle_.promise().takeResult();
    }

  private:
    Handle handle_;
    bool ownsHandle_{};
  };

  Task() = default;

  ~Task() noexcept {
    reset();
  }

  Task(const Task &) = delete ("Task<T> owns coroutine handle");
  auto operator=(const Task &) -> Task & = delete ("Task<T> owns coroutine handle");

  Task(Task &&other) noexcept
      : handle_(std::exchange(other.handle_, {})) {
  }

  auto operator=(Task &&other) noexcept -> Task & {
    if (this == std::addressof(other))
      return *this;

    reset();
    handle_ = std::exchange(other.handle_, {});
    return *this;
  }

  [[nodiscard]] auto valid() const noexcept -> bool {
    return static_cast<bool>(handle_);
  }

  [[nodiscard]] auto done() const noexcept -> bool {
    return not handle_ or handle_.done();
  }

  auto takeResult() && -> void {
    if (not handle_ or not handle_.done())
      throw std::logic_error{"Nyx::Test cannot read an incomplete Task<T>."};

    handle_.promise().takeResult();
  }

  [[nodiscard]] auto operator co_await() & noexcept -> Awaiter {
    return Awaiter{handle_, false};
  }

  [[nodiscard]] auto operator co_await() && noexcept -> Awaiter {
    return Awaiter{std::exchange(handle_, {}), true};
  }

private:
  template <class OtherValue, class Resume, class BeforeResume, class NextWakeUp>
  friend auto detail::drive(Task<OtherValue> &task,
      detail::RunLoop &runLoop,
      Resume &&resume,
      BeforeResume &&beforeResume,
      NextWakeUp &&nextWakeUp) -> void;

  explicit Task(Handle handle) noexcept
      : handle_(handle) {
  }

  auto reset() noexcept -> void {
    if (handle_)
      handle_.destroy();

    handle_ = {};
  }

  Handle handle_;
};

namespace detail {

template <class Value, class Resume, class BeforeResume, class NextWakeUp>
auto drive(Task<Value> &task,
    RunLoop &runLoop,
    Resume &&resume,
    BeforeResume &&beforeResume,
    NextWakeUp &&nextWakeUp) -> void {
  if (not task.valid())
    throw std::logic_error{"Nyx::Test cannot execute an empty Task<T>"};

  const auto discardPending = std::scope_exit([&runLoop] -> void { runLoop.discardPending(); });
  runLoop.enqueue(task.handle_);

  while (not task.done()) {
    if (const Option<std::coroutine_handle<>> handle = runLoop.dequeue()) {
      RunLoopBinding binding{runLoop};
      std::invoke(std::forward<BeforeResume>(beforeResume));
      std::invoke(std::forward<Resume>(resume), *handle);
      continue;
    }

    const RunLoop::WaitResult waitResult =
        runLoop.waitForWork(std::invoke(std::forward<NextWakeUp>(nextWakeUp)));
    if (waitResult == RunLoop::WaitResult::NoWork)
      break;

    if (waitResult == RunLoop::WaitResult::ExternalWake) {
      RunLoopBinding binding{runLoop};
      std::invoke(std::forward<BeforeResume>(beforeResume));
      if (runLoop.stopRequested())
        runLoop.wakeAllTimers();
    }
  }

  if (not task.done() and runLoop.stopRequested())
    throw TaskCancelled{};

  if (not task.done())
    throw TaskLifecycleError{TaskLifecycleFailure::Stranded};

  const usize pendingWork = runLoop.pendingWorkCount();
  if (pendingWork != 0)
    throw TaskLifecycleError{TaskLifecycleFailure::PendingWork, pendingWork};
}

template <class Value, class Resume, class BeforeResume, class NextWakeUp>
auto drive(Task<Value> &task,
    Resume &&resume,
    BeforeResume &&beforeResume,
    NextWakeUp &&nextWakeUp,
    std::stop_token stopToken) -> void {
  RunLoop runLoop{TimeMode::Real, std::move(stopToken)};
  drive(task,
      runLoop,
      std::forward<Resume>(resume),
      std::forward<BeforeResume>(beforeResume),
      std::forward<NextWakeUp>(nextWakeUp));
}

template <class Value, class Resume, class BeforeResume>
auto drive(Task<Value> &task, Resume &&resume, BeforeResume &&beforeResume, std::stop_token stopToken)
    -> void {
  drive(
      task,
      std::forward<Resume>(resume),
      std::forward<BeforeResume>(beforeResume),
      [] noexcept -> Option<RunLoop::TimePoint> { return None; },
      std::move(stopToken));
}

template <class Value, class Resume>
auto drive(Task<Value> &task, Resume &&resume) -> void {
  drive(task, std::forward<Resume>(resume), [] noexcept -> void {});
}

} // namespace detail

} // namespace Nyx::Test
// NOLINTEND(readability-identifier-naming)
