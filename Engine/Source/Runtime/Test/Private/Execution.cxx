module Nyx.Test;

import :Diagnostics;
import :Environment;
import :Execution;
import :Policies;

import std;
import Nyx.Core;

namespace Nyx::Test {

namespace {

[[nodiscard]] constexpr auto isPanicDiagnostic(const Diagnostic &diagnostic) noexcept -> bool {
  return diagnostic.header.code == DiagnosticCode::TestPanicked or
         diagnostic.header.code == DiagnosticCode::UnhandledException or
         diagnostic.header.code == DiagnosticCode::TestReturnedError;
}

[[nodiscard]] constexpr auto containsText(const DiagnosticNote &note, StringView expected) noexcept -> bool {
  return note.message.contains(expected) or
         std::ranges::any_of(
             note.fragments, [expected](const DiagnosticFragment &fragment) constexpr noexcept -> bool {
               return fragment.text.contains(expected);
             });
}

[[nodiscard]] constexpr auto diagnosticMatches(const Diagnostic &diagnostic, StringView expected) noexcept
    -> bool {
  if (not isPanicDiagnostic(diagnostic))
    return false;

  if (expected.empty() or diagnostic.description().contains(expected))
    return true;

  if (std::ranges::any_of(
          diagnostic.details.notes, [expected](const DiagnosticNote &note) constexpr noexcept -> bool {
            return containsText(note, expected);
          })) {
    return true;
  }

  return std::ranges::any_of(diagnostic.details.attachments,
      [expected](const DiagnosticAttachment &attachment) constexpr noexcept -> bool {
        return attachment.name.contains(expected) or attachment.content.contains(expected);
      });
}

[[nodiscard]] constexpr auto matchingPanicIndex(const TestState &state, StringView expected) noexcept
    -> Option<usize> {
  const auto matching = std::ranges::find_if(
      state.diagnostics, [expected](const Diagnostic &diagnostic) constexpr noexcept -> bool {
        return diagnosticMatches(diagnostic, expected);
      });
  if (matching == state.diagnostics.end())
    return None;

  return static_cast<usize>(std::ranges::distance(state.diagnostics.begin(), matching));
}

[[nodiscard]] auto durationText(std::chrono::steady_clock::duration duration) -> String {
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
  if (milliseconds.count() != 0)
    return std::format("{} ms", milliseconds.count());

  const auto microseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
  return std::format("{} μs", microseconds.count());
}

[[nodiscard]] auto expectedPanicDiagnostic(StringView expected, std::source_location location) -> Diagnostic {
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::ExpectedPanicNotObserved, location);
  diagnostic.details.spans.front().label = "expected panic";
  if (expected.empty())
    diagnostic.addNote("expected the test to panic");
  else
    diagnostic.addNote(std::format("expected a panic containing: {}", expected));

  return diagnostic;
}

[[nodiscard]] auto timeoutDiagnostic(std::chrono::steady_clock::duration limit,
    std::chrono::steady_clock::duration elapsed,
    std::source_location location) -> Diagnostic {
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::TimeoutExceeded, location);
  diagnostic.details.spans.front().label = "timeout";
  diagnostic.addNote(std::format("limit: {}", durationText(limit)));
  diagnostic.addNote(std::format("elapsed: {}", durationText(elapsed)));
  return diagnostic;
}

} // namespace

auto TestExecution::passed() const noexcept -> bool {
  return state.passed();
}

auto TestExecution::failed() const noexcept -> bool {
  return state.failed();
}

auto detail::returnedErrorDiagnostic(const Error &error, std::source_location location) -> Diagnostic {
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::TestReturnedError, location);
  diagnostic.details.spans.front().label = "test return";

  if (error.messages.empty()) {
    diagnostic.addNote("test returned an error without a message");
    return diagnostic;
  }

  std::ranges::for_each(error.messages, [&diagnostic](const Error::Message &message) constexpr -> void {
    const String text = message.message.empty() ? String{"error"} : message.message;
    diagnostic.addNote(
        text, DiagnosticLevel::Note, makeSpan("error origin", SpanKind::Secondary, message.location));
  });
  return diagnostic;
}

auto detail::unhandledExceptionDiagnostic(String message, std::source_location location) -> Diagnostic {
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::UnhandledException, location);
  diagnostic.details.spans.front().label = "test body";
  diagnostic.addNote(std::format("exception: {}", message));
  return diagnostic;
}

auto detail::panickedDiagnostic(String message, std::source_location location) -> Diagnostic {
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::TestPanicked, location);
  diagnostic.details.spans.front().label = "panic";
  diagnostic.addNote(std::format("panic: {}", message));
  return diagnostic;
}

auto detail::applyPolicy(const TestPolicy &policy,
    TestEnvironment &environment,
    std::chrono::steady_clock::duration elapsed,
    std::source_location location) -> void {
  if (policy.expectedPanic) {
    const Option<usize> index = matchingPanicIndex(environment.state(), *policy.expectedPanic);
    if (index) {
      environment.recordTrace(std::format("expected panic observed: {}", *policy.expectedPanic));
      environment.acceptExpectedPanic(*index);
    } else {
      environment.recordError(expectedPanicDiagnostic(*policy.expectedPanic, location));
    }
  }

  if (policy.timeout and elapsed > *policy.timeout)
    environment.recordError(timeoutDiagnostic(*policy.timeout, elapsed, location));
}

} // namespace Nyx::Test
