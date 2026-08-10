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

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

namespace detail {

using Deadline = Option<std::chrono::steady_clock::time_point>;

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

auto applyPolicy(const TestPolicy &policy,
    TestEnvironment &environment,
    std::chrono::steady_clock::duration elapsed,
    bool retry,
    bool cancelled,
    bool timeoutTriggered,
    std::source_location location) -> void;

template <class Value>
auto normalizeResult(const Result<Value> &result,
    TestEnvironment &environment,
    const Context &context,
    std::source_location location,
    RunLoop &runLoop,
    const Deadline &deadline) -> void;

template <class Value>
auto normalizeTask(Task<Value> &&task,
    TestEnvironment &environment,
    const Context &context,
    std::source_location location,
    RunLoop &runLoop,
    const Deadline &deadline) -> void;

template <class Return>
auto normalizeReturn(Return &&returned,
    TestEnvironment &environment,
    const Context &context,
    std::source_location location,
    RunLoop &runLoop,
    const Deadline &deadline) -> void {
  using Type = std::remove_cvref_t<Return>;

  if constexpr (is_task_return_v<Type>) {
    static_assert(not std::is_lvalue_reference_v<Return>,
        "Nyx::Test asynchronous test functions must return Task<T> by value.");
    normalizeTask(std::forward<Return>(returned), environment, context, location, runLoop, deadline);
  } else if constexpr (std::same_as<Type, Expression>) {
    static_cast<void>(check(std::forward<Return>(returned), location));
  } else if constexpr (is_result_return_v<Type>) {
    normalizeResult(returned, environment, context, location, runLoop, deadline);
  } else if constexpr (BoolTestable<Return>) {
    if (static_cast<bool>(std::forward<Return>(returned))) {
      environment.recordPass();
      return;
    }

    Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::AssertionFailed, location);
    diagnostic.header.descriptionOverride = "test returned false";
    diagnostic.details.spans.front().label = "test return";
    diagnostic.details.spans.front().selection = SpanSelection::Declaration;
    environment.recordFailure(std::move(diagnostic));
  } else {
    static_assert(meta::always_false_v<Type>,
        "Nyx::Test functions must return void, a bool-testable value, or Result<T>, or Task<T>.");
  }
}

template <class Value>
auto normalizeTask(Task<Value> &&task,
    TestEnvironment &environment,
    const Context &context,
    std::source_location location,
    RunLoop &runLoop,
    const Deadline &deadline) -> void {
  const auto requestTimeoutStop = [&environment, &runLoop, &deadline] -> void {
    if (deadline and not environment.stopRequested() and runLoop.now() >= *deadline)
      environment.requestStop();
  };

  const auto nextTimeoutWake = [&environment, &deadline] -> Deadline {
    if (environment.stopRequested())
      return None;

    return deadline;
  };

  detail::drive(
      task,
      runLoop,
      [&environment, &context](std::coroutine_handle<> handle) -> void {
        EnvironmentBinding environmentBinding{environment};
        ContextBinding contextBinding{context};
        profiling::SinkBinding profileBinding{environment.profileSink()};
        handle.resume();
      },
      requestTimeoutStop,
      nextTimeoutWake);

  if constexpr (std::same_as<Value, void>) {
    std::move(task).takeResult();
  } else {
    normalizeReturn(std::move(task).takeResult(), environment, context, location, runLoop, deadline);
  }
}

template <class Value>
auto normalizeResult(const Result<Value> &result,
    TestEnvironment &environment,
    const Context &context,
    std::source_location location,
    RunLoop &runLoop,
    const Deadline &deadline) -> void {
  if (not result) {
    environment.recordError(returnedErrorDiagnostic(result.error(), location));
    return;
  }

  if constexpr (std::is_same_v<Value, void>)
    return;
  else
    normalizeReturn(*result, environment, context, location, runLoop, deadline);
}

template <class Function>
auto invokeTest(Function &&function, const Context &context) -> decltype(auto) {
  if constexpr (ContextInvocable<Function>)
    return std::invoke(std::forward<Function>(function), context);
  else
    return std::invoke(std::forward<Function>(function));
}

} // namespace detail

/// Executes one test in a dynamically bound TestEnvironment.
///
/// A TestAbort records a fatal requirement failure but is not itself an error.
/// Other exceptions are converted into UnhandledException diagnostics. Coroutine timeouts request
/// Context::stopToken at the next queued resume. TimeMode::Virtual advances only Nyx::Test scheduler time.
template <detail::TestInvocable Function>
[[nodiscard]]
auto run(TestDescriptor descriptor, // NOLINT(readability-function-size)
    Function &&function,
    TimeMode timeMode = TimeMode::Real) -> TestExecution {
  const detail::InvocationSettings invocation = detail::currentInvocationSettings();
  const auto wallStarted = std::chrono::steady_clock::now();
  const Option<memory::ProcessMemorySnapshot> memoryBefore =
      invocation.captureMemory ? memory::processMemory() : None;
  TestEnvironment environment{};
  TestExecution execution{
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
  };
  const auto finalizeEnvironment = std::scope_exit(
      [&environment, &execution] -> void { environment.finalize(execution.descriptor.location); });

  if (execution.descriptor.name.empty())
    execution.descriptor.name = execution.descriptor.identifier;

  if (execution.descriptor.policy.trace or invocation.forceTrace)
    environment.enableTrace();

  environment.recordTrace(std::format("enabled tracing for: {} ...", execution.descriptor.name));
  detail::RunLoop runLoop{timeMode, environment.stopToken()};
  const detail::RunLoop::TimePoint started = runLoop.now();
  detail::Deadline deadline{};
  if (execution.descriptor.policy.timeout)
    deadline.emplace(started + *execution.descriptor.policy.timeout);
  bool cancelled{};

  {
    const auto recordDuration =
        std::scope_exit([&execution, &runLoop] -> void { execution.duration = runLoop.elapsed(); });
    const Context context{
        .name = StringView{execution.descriptor.name},
        .description = StringView{execution.descriptor.description},
        .testCase = execution.descriptor.testCase,
        .resources = environment.resources(),
        .seed = execution.seed,
        .iteration = execution.iteration,
        .sample = invocation.sample,
        .retry = invocation.retry,
        .warmup = invocation.warmup,
        .stopToken = environment.stopToken(),
        .location = execution.descriptor.location,
    };

    EnvironmentBinding environmentBinding{environment};
    ContextBinding contextBinding{context};
    profiling::SinkBinding profileBinding{environment.profileSink()};
    auto testProfile = profiling::profileScope(execution.descriptor.name, execution.descriptor.location);

    try {
      using Return = decltype(detail::invokeTest(std::forward<Function>(function), context));
      using ReturnType = std::remove_cvref_t<Return>;

      if constexpr (std::same_as<Return, void>) {
        EnvironmentBinding binding{environment};
        ContextBinding contextBinding{context};
        detail::invokeTest(std::forward<Function>(function), context);
      } else if constexpr (detail::is_task_return_v<ReturnType>) {
        static_assert(not std::is_lvalue_reference_v<ReturnType>,
            "Nyx::Test asynchronous test functions must return Task<T> by value.");
        const auto invokeTask = [&] -> ReturnType {
          EnvironmentBinding binding{environment};
          ContextBinding contextBinding{context};
          return detail::invokeTest(std::forward<Function>(function), context);
        };
        detail::normalizeReturn(
            invokeTask(), environment, context, execution.descriptor.location, runLoop, deadline);
      } else {
        EnvironmentBinding binding{environment};
        ContextBinding contextBinding{context};
        detail::normalizeReturn(detail::invokeTest(std::forward<Function>(function), context),
            environment,
            context,
            execution.descriptor.location,
            runLoop,
            deadline);
      }
    } catch (const detail::TaskCancelled &) { // NOLINT
      // The timeout policy records the final diagnostic after duration capture.
      cancelled = true;
    } catch (const detail::TaskLifecycleError &error) {
      environment.recordError(detail::taskLifecycleDiagnostic(error, execution.descriptor.location));
    } catch (const TestAbort &) { // NOLINT
      // require() already recorded the fatal assertion and marked the test aborted.
      // nothing to do here.
    } catch (const TestPanic &panicException) {
      environment.recordError(
          detail::panickedDiagnostic(String{panicException.message()}, panicException.location()));
    } catch (const std::exception &exception) {
      const char *message = exception.what();
      environment.recordError(detail::unhandledExceptionDiagnostic(
          message == nullptr ? String{"standard exception"} : String{message},
          execution.descriptor.location));
    } catch (...) {
      environment.recordError(
          detail::unhandledExceptionDiagnostic("non-standard exception", execution.descriptor.location));
    }
  }

  execution.wallDuration = std::chrono::steady_clock::now() - wallStarted;
  execution.profile = environment.profileSnapshot();
  detail::applyPolicy(execution.descriptor.policy,
      environment,
      execution.duration,
      invocation.retry != 0,
      cancelled,
      runLoop.timeoutTriggered(),
      execution.descriptor.location);
  environment.finalize(execution.descriptor.location);
  execution.resources = environment.resourceSnapshot();
  execution.memoryBefore = memoryBefore;
  execution.memoryAfter = invocation.captureMemory ? memory::processMemory() : None;
  execution.state = std::move(environment).takeState();

  return execution;
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

/// Executes one test with an explicit per-run time source.
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
