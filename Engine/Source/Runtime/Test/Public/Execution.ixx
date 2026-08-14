export module Nyx.Test:Execution;

import std;
import Nyx.Core;
import :Assertions;
import :Context;
import :Diagnostics;
import :Environment;
import :Expressions;
import :Policies;
import :Task;

export namespace Nyx::Test {

/// Immutable reflected labels attached to an expanded test case.
struct TestMetadata final {
  Option<String> group;
  Vec<String> tags;
};

struct TestDescriptor final {
  String identifier;
  std::source_location location;
  String name;
  String description;
  usize testCase{};
  TestPolicy policy{};
  TestMetadata metadata{};
};

struct AttemptIndex final {
  usize runIteration{};
  usize sample{};
  usize retry{};

  [[nodiscard]] constexpr auto operator==(const AttemptIndex &) const noexcept -> bool = default;
};

/// Identifies the native mechanism that terminated an isolated worker.
enum class[[= debug::derive]] NativeFaultKind : u8 {
  Signal[[= debug::rename("signal")]],
  StructuredException[[= debug::rename("structured_exception")]],
  Terminated[[= debug::rename("terminated")]],
  IsolationUnavailable[[= debug::rename("isolation_unavailable")]],
};

/// Identifies the portable class of a POSIX signal that terminated a test worker.
enum class[[= debug::derive]] NativeSignal : u8 {
  Unknown[[= debug::rename("unknown signal")]],
  Abort[[= debug::rename("abort")]],
  BusError[[= debug::rename("bus error")]],
  FloatingPointException[[= debug::rename("floating-point exception")]],
  IllegalInstruction[[= debug::rename("illegal instruction")]],
  SegmentationFault[[= debug::rename("segmentation fault")]],
  Trap[[= debug::rename("trace or breakpoint Trap")]],
};

/// Minimal, allocation-free fault data collected by a worker boundary.
struct NativeFault final {
  NativeFaultKind kind{NativeFaultKind::Terminated};
  NativeSignal signal{NativeSignal::Unknown};
  i32 code{};
  u64 address{};
  u64 instruction{};
  bool symbolsAvailable{};
};

struct TestExecution final {
  TestDescriptor descriptor;
  TestState state;
  std::chrono::steady_clock::duration duration{};
  std::chrono::steady_clock::duration wallDuration{};
  profiling::ProfileSnapshot profile;
  ResourceSnapshot resources;
  Option<memory::ProcessMemorySnapshot> memoryBefore;
  Option<memory::ProcessMemorySnapshot> memoryAfter;
  /// Root seed selected for the whole RunOptions invocation.
  u64 runSeed{};
  /// Per-case seed exposed through Context::seed.
  u64 seed{};
  /// Zero-based repeat index for this execution.
  usize iteration{};
  AttemptIndex attempt{};
  bool warmup{};
  TraceMode traceMode{TraceMode::Annotations};
  /// Is set when the parent reconstructs a terminal native fault from an isolated worker.
  Option<NativeFault> fault;

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

namespace detail {

using Deadline = Option<std::chrono::steady_clock::time_point>;

/// Carries the stable shared by every return-value normalization step in one attempt.
struct NormalizationContext final { // NOLINT(cppcoreguidelines-pro-type-member-init)
  Ref<TestEnvironment> environment;
  Ref<const Context> context;
  std::source_location location;
  Ref<RunLoop> runLoop;
  Ref<const Deadline> deadline;
};

/// Groups the policy inputs that are produced while an attempt is being completed.
struct PolicyApplication final { // NOLINT(cppcoreguidelines-pro-type-member-init)
  Ref<const TestPolicy> policy;
  Ref<TestEnvironment> environment;
  std::chrono::steady_clock::duration elapsed{};
  bool retry{};
  bool cancelled{};
  bool timeoutTriggered{};
  std::source_location location;
};

// NOLINTBEGIN(readability-identifier-naming)
template <class>
inline constexpr bool is_result_return_v{};

template <class Value>
inline constexpr bool is_result_return_v<Result<Value>>{true};

template <class>
inline constexpr bool is_task_return_v{};

template <class Value>
inline constexpr bool is_task_return_v<Task<Value>>{true};
// NOLINTEND(readability-identifier-naming)

template <class Function>
concept ContextInvocable = std::invocable<Function, const Context &>;

template <class Function>
concept TestInvocable = std::invocable<Function> or ContextInvocable<Function>;

[[nodiscard]] auto returnedErrorDiagnostic(const Error &error, std::source_location location) -> Diagnostic;

[[nodiscard]] auto unhandledExceptionDiagnostic(String message, std::source_location location) -> Diagnostic;

[[nodiscard]] auto panickedDiagnostic(String message, std::source_location location) -> Diagnostic;

[[nodiscard]] auto taskLifecycleDiagnostic(const TaskLifecycleError &error, std::source_location location)
    -> Diagnostic;

[[nodiscard]] auto nativeFaultDiagnostic(const NativeFault &fault, std::source_location location)
    -> Diagnostic;

auto applyPolicy(const PolicyApplication &application) -> void;

template <class Value>
auto normalizeResult(const Result<Value> &result, NormalizationContext &context) -> void;

template <class Value>
auto normalizeTask(Task<Value> &&task, NormalizationContext &context) -> void;

template <class Return>
auto normalizeReturn(Return &&returned, NormalizationContext &context) -> void {
  using Type = std::remove_cvref_t<Return>;

  if constexpr (is_task_return_v<Type>) {
    static_assert(not std::is_lvalue_reference_v<Return>,
        "Nyx::Test asynchronous test functions must return Task<T> by value.");
    normalizeTask(std::forward<Return>(returned), context);
  } else if constexpr (std::same_as<Type, Expression>) {
    static_cast<void>(check(std::forward<Return>(returned), context.location));
  } else if constexpr (is_result_return_v<Type>) {
    normalizeResult(returned, context);
  } else if constexpr (BoolTestable<Return>) {
    if (static_cast<bool>(std::forward<Return>(returned))) {
      context.environment.get().recordPass();
      return;
    }

    Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::AssertionFailed, context.location);
    diagnostic.header.descriptionOverride = "test returned false";
    diagnostic.details.spans.front().label = "test return";
    diagnostic.details.spans.front().selection = SpanSelection::Declaration;
    context.environment.get().recordFailure(std::move(diagnostic));
  } else {
    static_assert(meta::always_false_v<Type>,
        "Nyx::Test functions must return void, a bool-testable value, or Result<T>, or Task<T>.");
  }
}

/// Re-established all task-local bindings before a coroutine frame resumes.
auto resumeWithBindings(TestEnvironment &environment, const Context &context, std::coroutine_handle<> handle)
    -> void {
  EnvironmentBinding environmentBinding{environment};
  ContextBinding contextBinding{context};
  profiling::SinkBinding profileBinding{environment.profileSink()};
  handle.resume();
}

/// Callback used by the scheduler to restore task-local state before each resume.
struct ResumeCallback final {
  Ref<TestEnvironment> environment;
  Ref<const Context> context;

  auto operator()(std::coroutine_handle<> handle) const -> void {
    resumeWithBindings(environment.get(), context.get(), handle);
  }
};

/// Requests cancellation once the scheduler reaches the attempt deadline.
struct TimeoutStopCallback final {
  Ref<NormalizationContext> context;

  auto operator()() const -> void {
    NormalizationContext &ctx = context.get();
    const Deadline &deadline = ctx.deadline.get();
    TestEnvironment &environment = ctx.environment.get();
    RunLoop &runLoop = ctx.runLoop.get();

    if (deadline and not environment.stopRequested() and
        (runLoop.timeoutTriggered() or runLoop.now() > *deadline))
      environment.requestStop();
  }
};

/// Supplies the scheduler with the next timeout boundary, unless cancellation already won the race.
struct TimeoutWakeCallback final {
  Ref<const NormalizationContext> context;

  [[nodiscard]] auto operator()() const -> Deadline {
    const NormalizationContext &ctx = context.get();

    if (ctx.environment.get().stopRequested())
      return None;

    return ctx.deadline.get();
  }
};

template <class Value>
auto normalizeTask(Task<Value> &&task, NormalizationContext &context) -> void {
  const TaskDriveResult result = detail::drive(task,
      context.runLoop,
      ResumeCallback{.environment = context.environment, .context = context.context},
      TimeoutStopCallback{context},
      TimeoutWakeCallback{context});

  TestEnvironment &environment = context.environment.get();

  switch (result.status) {
    case TaskDriveStatus::Completed: break;
    case TaskDriveStatus::Cancelled: environment.requestStop(); return;
    case TaskDriveStatus::Empty:
      environment.recordError(
          detail::taskLifecycleDiagnostic(TaskLifecycleError{TaskLifecycleFailure::Empty}, context.location));
      return;
    case TaskDriveStatus::Stranded:
      environment.recordError(detail::taskLifecycleDiagnostic(
          TaskLifecycleError{TaskLifecycleFailure::Stranded}, context.location));
      return;
    case TaskDriveStatus::PendingWork:
      environment.recordError(detail::taskLifecycleDiagnostic(
          TaskLifecycleError{TaskLifecycleFailure::PendingWork, result.pendingWork}, context.location));
      return;
  }

  if constexpr (std::same_as<Value, void>) {
    std::move(task).takeResult();
  } else {
    normalizeReturn(std::move(task).takeResult(), context);
  }
}

template <class Value>
auto normalizeResult(const Result<Value> &result, NormalizationContext &context) -> void {
  if (not result) {
    context.environment.get().recordError(returnedErrorDiagnostic(result.error(), context.location));
    return;
  }

  if constexpr (std::is_same_v<Value, void>)
    return;
  else
    normalizeReturn(*result, context);
}

template <class Function>
auto invokeTest(Function &&function, const Context &context) -> decltype(auto) {
  if constexpr (ContextInvocable<Function>)
    return std::invoke(std::forward<Function>(function), context);
  else
    return std::invoke(std::forward<Function>(function));
}

/// Owns the mutable state of one physical attempt from setup through final reporting.
///
/// The object deliberately keeps the environment and scheduler together: the scheduler's stop token belongs
/// to this environment, and every Context created for the attempt refers to the same resource arena.
struct ActiveExecution final {
  TestExecution execution;
  std::chrono::steady_clock::time_point wallStarted;
  Option<memory::ProcessMemorySnapshot> memoryBefore;
  TestEnvironment environment;
  RunLoop runLoop;
  Deadline deadline;
  bool canceled{};

  ActiveExecution(TestDescriptor descriptor, const InvocationSettings &invocation, TimeMode timeMode)
      : execution{
            .descriptor = std::move(descriptor),
            .seed = invocation.seed,
            .iteration = invocation.iteration,
            .attempt =
                AttemptIndex{
                    .runIteration = invocation.iteration,
                    .sample = invocation.sample,
                    .retry = invocation.retry,
                },
            .warmup = invocation.warmup,
        },
  wallStarted(std::chrono::steady_clock::now()), memoryBefore(invocation.captureMemory ? memory::processMemory() : None), runLoop{timeMode, environment.stopToken()}{
  }

  auto prepare(const InvocationSettings &invocation) -> void {
    if (execution.descriptor.name.empty())
      execution.descriptor.name = execution.descriptor.identifier;

    if (execution.descriptor.policy.trace or invocation.forceTrace)
      environment.enableTrace();

    environment.recordTrace(std::format("enabled tracing for: {} ...", execution.descriptor.name));

    if (execution.descriptor.policy.timeout)
      deadline.emplace(runLoop.now() + *execution.descriptor.policy.timeout);
  }
};

[[nodiscard]] auto makeContext(ActiveExecution &active, const InvocationSettings &invocation) -> Context {
  return Context{
      .name = active.execution.descriptor.name,
      .description = active.execution.descriptor.description,
      .testCase = active.execution.descriptor.testCase,
      .resources = active.environment.resources(),
      .seed = active.execution.seed,
      .iteration = active.execution.iteration,
      .sample = invocation.sample,
      .retry = invocation.retry,
      .warmup = invocation.warmup,
      .stopToken = active.environment.stopToken(),
      .location = active.execution.descriptor.location,
  };
}

template <class Function>
auto invokeBody(ActiveExecution &active, const Context &context, Function &&function) -> void {
  using Return = decltype(invokeTest(std::forward<Function>(function), context));
  using ReturnType = std::remove_cvref_t<Return>;
  NormalizationContext normalization{
      .environment = active.environment,
      .context = context,
      .location = active.execution.descriptor.location,
      .runLoop = active.runLoop,
      .deadline = active.deadline,
  };

  if constexpr (std::same_as<Return, void>) {
    invokeTest(std::forward<Function>(function), context);
  } else {
    if constexpr (is_task_return_v<ReturnType>)
      static_assert(not std::is_lvalue_reference_v<Return>,
          "Nyx::Test asynchronous test functions must return Task<T> by value.");
    normalizeReturn(invokeTest(std::forward<Function>(function), context), normalization);
  }
}

auto recordException(ActiveExecution &active, const TestPanic &exception) -> void {
  active.environment.recordError(panickedDiagnostic(exception.what(), exception.location()));
}

auto recordException(ActiveExecution &active, const std::exception &exception) -> void {
  const char *message = exception.what();
  active.environment.recordError(unhandledExceptionDiagnostic(
      message != nullptr ? message : "standard exception", active.execution.descriptor.location));
}

auto recordException(ActiveExecution &active) -> void {
  active.environment.recordError(
      unhandledExceptionDiagnostic("non-standard exception", active.execution.descriptor.location));
}

template <class Function>
auto invokeBodySafely(ActiveExecution &active, const Context &context, Function &&function) -> void {
  try {
    invokeBody(active, context, std::forward<Function>(function));
  } catch (const TestAbort &) { // NOLINT(bugprone-empty-catch)
    // require() has already recorded the fatal assertion and marked this attempt as aborted.
  } catch (const TestPanic &exception) {
    recordException(active, exception);
  } catch (const std::exception &exception) {
    recordException(active, exception);
  } catch (...) {
    recordException(active);
  }
}

[[nodiscard]] auto completeExecution(ActiveExecution &active, const InvocationSettings &invocation)
    -> TestExecution {
  active.execution.duration = active.runLoop.elapsed();
  active.execution.wallDuration = std::chrono::steady_clock::now() - active.wallStarted;
  active.execution.profile = active.environment.profileSnapshot();
  detail::applyPolicy(detail::PolicyApplication{
      .policy = active.execution.descriptor.policy,
      .environment = active.environment,
      .elapsed = active.execution.duration,
      .retry = invocation.retry != 0,
      .cancelled = active.canceled,
      .timeoutTriggered = active.runLoop.timeoutTriggered(),
      .location = active.execution.descriptor.location,
  });
  active.environment.finalize(active.execution.descriptor.location);
  active.execution.resources = active.environment.resourceSnapshot();
  active.execution.memoryBefore = active.memoryBefore;
  active.execution.memoryAfter = invocation.captureMemory ? memory::processMemory() : None;
  active.execution.state = std::move(active.environment).takeState();
  return std::move(active.execution);
}

} // namespace detail

/// Executes one test in a dynamically bound TestEnvironment.
///
/// A detail::TestAbort records a fatal requirement failure but is not itself an error.
/// Other exceptions are converted into UnhandledException diagnostics. Coroutine timeouts request
/// Context::stopToken at the next queued resume. TimeMode::Virtual advances only Nyx::Test scheduler time.
template <detail::TestInvocable Function>
[[nodiscard]]
auto run(TestDescriptor descriptor, Function &&function, TimeMode timeMode = TimeMode::Real)
    -> TestExecution {
  if constexpr (build::tests) {
    const detail::InvocationSettings invocation = detail::currentInvocationSettings();
    detail::ActiveExecution active{std::move(descriptor), invocation, timeMode};
    const std::source_location location = active.execution.descriptor.location;
    const auto finalizeEnvironment =
        std::scope_exit([&active, location] -> void { active.environment.finalize(location); });
    active.prepare(invocation);
    const Context context = detail::makeContext(active, invocation);

    {
      EnvironmentBinding environmentBinding{active.environment};
      ContextBinding contextBinding{context};
      profiling::SinkBinding profileBinding{active.environment.profileSink()};
      auto testProfile =
          profiling::profileScope(active.execution.descriptor.name, active.execution.descriptor.location);
      detail::invokeBodySafely(active, context, std::forward<Function>(function));
      active.canceled = active.environment.stopRequested();
    }

    return detail::completeExecution(active, invocation);
  } else {
    return TestExecution{.descriptor = std::move(descriptor)};
  }
}

template <detail::TestInvocable Function>
[[nodiscard]] auto run(StringView identifier,
    Function &&function,
    std::source_location location = std::source_location::current()) -> TestExecution {
  return run(
      TestDescriptor{
          .identifier = String{identifier},
          .location = location,
      },
      std::forward<Function>(function));
}

template <detail::TestInvocable Function>
[[nodiscard]] auto run(StringView identifier,
    Function &&function,
    TimeMode timeMode,
    std::source_location location = std::source_location::current()) -> TestExecution {
  return run(
      TestDescriptor{
          .identifier = String{identifier},
          .location = location,
      },
      std::forward<Function>(function),
      timeMode);
}

} // namespace Nyx::Test
