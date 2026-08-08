import std;

import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::expressions {

auto containsHighlighted(const Vec<DiagnosticFragment> &fragments, StringView text) -> bool {
  return std::ranges::any_of(fragments, [text](const DiagnosticFragment &fragment) -> bool {
    return fragment.highlighted and fragment.text == text;
  });
}

[[ = test, = group("framework"), = tag("expressions") ]] auto compactLiteralsUseTheAssertionLocation()
    -> void {
  const auto location = std::source_location::current();

  Diagnostic diagnostic{};

  {
    constexpr u32 value{2};
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};
    const Expression expression = value == 3_exp;
    check(expression, location);
    diagnostic = environment.state().diagnostics.front();
  };

  check(eq(diagnostic.details.spans.front().location.line(), location.line()));
  check(diagnostic.details.spans.front().label == "assertion"_exp);
}

[[ = test, = group("framework"), = tag("expressions") ]] auto literalOperatorsProduceExpressions() -> void {
  constexpr u32 one{1};
  constexpr u32 two{2};
  constexpr u32 three{3};

  check(two == 2_exp);
  check(two != 3_exp);
  check(one < 2_exp);
  check(three > 2_exp);
  check(two <= 2_exp);
  check(two >= 2_exp);
}

[[ = test, = group("framework"), = tag("expressions") ]] auto stringDifferencesVisualizeWhitespace() -> void {
  Vec<DiagnosticNote> notes{};

  {
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};
    const auto location = std::source_location::current();

    static_cast<void>(check(eq(StringView{"Nyx Test"}, "Nyx  Test"_exp, location)));

    notes = environment.state().diagnostics.front().details.notes;
  };

  require(notes.size() == 3);
  check(containsHighlighted(notes[1].fragments, "∅"));
  check(containsHighlighted(notes.back().fragments, "·"));
}

[[ = test, = group("framework"), = tag("expressions") ]] auto utilityComparatorsProduceExpressions() -> void {
  TestState state{};
  Expression rangeFailure{};

  {
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};

    check(neq(2, 3));
    check(less(2, 3));
    check(greater(3, 2));
    check(lessOrEqual(3, 3));
    check(greaterOrEqual(3, 3));
    check(near(1.0, 1.01, 0.1));
    check(contains(StringView{"Nyx.Test"}, "Test"_exp));
    check(contains(Vec<u32>{1, 2, 3}, 2_exp));
    check(near(1.0, 1.2, 0.1));
    check(contains(StringView{"Nyx"}, "Test"_exp));
    rangeFailure = contains(Vec<u32>{1, 2, 3}, 4_exp);

    state = environment.state();
  }

  require(state.assertions == 10_exp);
  require(state.failedAssertions == 2_exp);
  require(state.diagnostics.size() == 2_exp);
  check(state.diagnostics.front().description() == "assertion failed"_exp);
  check(state.diagnostics.front().details.notes.front().message ==
        "condition: values are not within tolerance"_exp);
  check(state.diagnostics.back().details.notes.front().message ==
        "condition: value does not contain the expected element"_exp);
  check(rangeFailure.diagnostic);
  check(rangeFailure.diagnostic->details.notes[1].message == "range: [1, 2, 3]"_exp);
  check(rangeFailure.diagnostic->details.notes.back().message == "expected: 4"_exp);
}

[[ = test, = group("framework"), = tag("expressions") ]] auto requiredExpressionsAbortTheTest() -> void {
  TestState state{};
  bool continued{};

  {
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};

    try {
      require(eq(2, 3));
      continued = true;
    } catch (const TestAbort &) { // NOLINT(bugprone-empty-catch)
    }
    state = environment.state();
  }

  require(not continued);
  require(state.aborted);
  require(state.failedAssertions == 1_exp);
  check(state.diagnostics.front().description() == "requirement failed"_exp);
  check(state.diagnostics.front().details.notes.front().message == "condition: values are not equal"_exp);
  check(state.diagnostics.front().details.spans.front().label == "requirement"_exp);
}

[[ = test, = group("framework"), = tag("expressions") ]] auto returnedExpressionsPreserveTheirDiagnostics()
    -> void {
  const TestExecution execution = run("returnsExpression", [] -> Expression { return eq(u32{2}, 3_exp); });

  require(execution.failed());
  require(execution.state.failedAssertions == 1_exp);
  require(execution.state.errors == 0_exp);
  check(execution.state.diagnostics.front().description() == "assertion failed"_exp);
  check(execution.state.diagnostics.front().details.notes.front().message ==
        "condition: values are not equal"_exp);
}

[[ = test, = group("framework"), = tag("expressions") ]] auto comparisonsExposeValuesAndLocations() -> void {
  const auto location = std::source_location::current();
  bool passed{};
  bool failed{};
  TestState state{};

  {
    constexpr u32 value{2};
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};

    passed = check(eq(value, 2_exp, location));
    failed = check(eq(capture("answer", value), 3_exp, location));
    state = environment.state();
  };

  require(passed);
  require(not failed);
  require(state.assertions == 2_exp);
  require(state.failedAssertions == 1_exp);
  require(state.diagnostics.size() == 1_exp);
  check(state.diagnostics.front().description() == "assertion failed"_exp);
  check(state.diagnostics.front().details.spans.front().location.line() == location.line());
  check(state.diagnostics.front().details.spans.front().label == "assertion"_exp);
  check(state.diagnostics.front().details.notes.front().message == "condition: values are not equal"_exp);
  check(state.diagnostics.front().details.notes[1].message == "answer: 2"_exp);
  check(state.diagnostics.front().details.notes.back().message == "right: 3"_exp);
}

} // namespace Tests
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::expressions>();
}
