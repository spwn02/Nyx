export module Nyx.Test:Execution;

import std;
import Nyx.Core;
import :Assertions;
import :Context;
import :Diagnostics;
import :Environment;
import :Expressions;
import :Policies;

export namespace Nyx::Test {

struct TestDescriptor final {
  String identifier;
  std::source_location location;
  String name;
  String description;
  usize testCase{};
  TestPolicy policy{};
};

struct TestExecution final {
  TestDescriptor descriptor;
  TestState state;
  std::chrono::steady_clock::duration duration{};

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

namespace detail {

// NOLINTBEGIN(readability-identifier-naming)
template <class>
inline constexpr bool is_result_return_v{};

template <class Value>
inline constexpr bool is_result_return_v<Result<Value>>{true};

template <class>
inline constexpr bool dependent_false_v{};
// NOLINTEND(readability-identifier-naming)

template <class Function>
concept ContextInvocable = std::invocable<Function, const Context &>;

template <class Function>
concept TestInvocable = std::invocable<Function> or ContextInvocable<Function>;

[[nodiscard]] auto returnedErrorDiagnostic(const Error &error, std::source_location location) -> Diagnostic;

[[nodiscard]] auto unhandledExceptionDiagnostic(String message, std::source_location location) -> Diagnostic;

[[nodiscard]] auto panickedDiagnostic(String message, std::source_location location) -> Diagnostic;

auto applyPolicy(const TestPolicy &policy,
    TestEnvironment &environment,
    std::chrono::steady_clock::duration elapsed,
    std::source_location location) -> void;

template <class Value>
auto normalizeResult(const Result<Value> &result, TestEnvironment &environment, std::source_location location)
    -> void;

template <class Return>
auto normalizeReturn(Return &&returned, TestEnvironment &environment, std::source_location location) -> void {
  using Type = std::remove_cvref_t<Return>;

  if constexpr (std::same_as<Type, Expression>) {
    static_cast<void>(check(std::forward<Return>(returned), location));
  } else if constexpr (is_result_return_v<Type>) {
    normalizeResult(returned, environment, location);
  } else if constexpr (BoolTestable<Return>) {
    if (static_cast<bool>(std::forward<Return>(returned))) {
      environment.recordPass();
      return;
    }

    Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::AssertionFailed, location);
    diagnostic.header.descriptionOverride = "test returned false";
    diagnostic.details.spans.front().label = "test return";
    environment.recordFailure(std::move(diagnostic));
  } else {
    static_assert(dependent_false_v<Type>,
        "Nyx::Test functions must return void, a bool-testable value, or Result<T>.");
  }
}

template <class Value>
auto normalizeResult(const Result<Value> &result, TestEnvironment &environment, std::source_location location)
    -> void {
  if (not result) {
    environment.recordError(returnedErrorDiagnostic(result.error(), location));
    return;
  }

  if constexpr (std::is_same_v<Value, void>)
    return;
  else
    normalizeReturn(*result, environment, location);
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
/// Other exceptions are converted into UnhandledException diagnostics.
template <class Function>
[[nodiscard]]
auto run(TestDescriptor descriptor, Function &&function) -> TestExecution {
  TestEnvironment environment{};
  TestExecution execution{
      .descriptor = std::move(descriptor),
  };

  if (execution.descriptor.name.empty())
    execution.descriptor.name = execution.descriptor.identifier;

  if (execution.descriptor.policy.trace)
    environment.enableTrace();

  environment.recordTrace(std::format("Running test: {} ...", execution.descriptor.name));

  const std::chrono::time_point started = std::chrono::steady_clock::now();

  {
    const auto recordDuration = std::scope_exit(
        [&execution, started] -> void { execution.duration = std::chrono::steady_clock::now() - started; });

    {
      EnvironmentBinding binding{environment};
      const Context context{
          .name = StringView{execution.descriptor.name},
          .description = StringView{execution.descriptor.description},
          .testCase = execution.descriptor.testCase,
          .location = execution.descriptor.location,
      };
      ContextBinding contextBinding{context};

      try {
        using Return = decltype(detail::invokeTest(std::forward<Function>(function), context));

        if constexpr (std::same_as<Return, void>)
          detail::invokeTest(std::forward<Function>(function), context);
        else
          detail::normalizeReturn(detail::invokeTest(std::forward<Function>(function), context),
              environment,
              execution.descriptor.location);
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

    detail::applyPolicy(
        execution.descriptor.policy, environment, execution.duration, execution.descriptor.location);
    execution.state = std::move(environment).takeState();
  }

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

} // namespace Nyx::Test
