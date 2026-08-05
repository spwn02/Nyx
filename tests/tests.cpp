import std;

import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace PassingTests {

constexpr auto fibonacci(u32 input) -> u32 {
  switch (input) {
    case 0: return 0;
    case 1: return 1;
    default: return fibonacci(input - 2) + fibonacci(input - 1);
  }
}

[[ = test, = Case{0, 0}, = Case{1, 1}, = Case{5, 5} ]] constexpr auto fibonacciCases(u32 input, u32 expected)
    -> bool {
  return fibonacci(input) == expected;
}

[[= test]] auto voidCase() -> void {
  require(true);
}

auto ignored() -> bool {
  return false;
}

} // namespace PassingTests

namespace FailingTests {

[[ = test, = Case{2, 2}, = Case{3, 2} ]] constexpr auto identityCases(u32 input, u32 expected) -> bool {
  return input == expected;
}

} // namespace FailingTests

namespace FailingProviderSubjects {

[[ = test, = arg<"path">(files<"__nyx_missing_provider__/**/*.md">()) ]] auto receivesNoFiles(
    const Path &path) -> void {
  require(path.empty());
}

} // namespace FailingProviderSubjects

namespace ParallelTests {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> active{};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> peak{};

auto reset() -> void {
  active.store(0, std::memory_order_relaxed);
  peak.store(0, std::memory_order_relaxed);
}

auto observePeak(usize value) -> void {
  usize observed = peak.load(std::memory_order_relaxed);
  while (observed < value and not peak.compare_exchange_weak(observed, value, std::memory_order_relaxed)) {
    // pass
  }
}

[[ = test, = Case{1}, = Case{2}, = Case{3}, = Case{4} ]] auto runsConcurrently(u32 /*ignored*/)
    -> Task<void> {
  const usize current = active.fetch_add(1, std::memory_order_relaxed) + 1;
  observePeak(current);
  const auto cleanup = std::scope_exit([] -> void { active.fetch_sub(1, std::memory_order_relaxed); });

  co_await yield();
  std::this_thread::sleep_for(std::chrono::milliseconds{10});
  require(current <= 2_exp);
}

} // namespace ParallelTests

namespace Tests {

namespace diagnostics {

auto makeDiagnosticForRendering() -> Diagnostic {
  constexpr usize highlightedColumns{4};
  const auto location = std::source_location::current();
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::AssertionFailed, location);
  diagnostic.details.spans.clear();
  diagnostic.addSpan(makeSpan(
      "left value", SpanKind::Primary, location, static_cast<usize>(location.column()) + highlightedColumns));
  diagnostic.addNote("left: 2");
  diagnostic.addNote("right: 3");
  diagnostic.addNote(Vec<DiagnosticFragment>{
      DiagnosticFragment{.text = "difference: "},
      DiagnosticFragment{.text = "3", .highlighted = true},
  });
  diagnostic.addAttachment("input", "2 != 3");

  return diagnostic;
}
[[= test]] auto diagnostics() -> void {
  const Diagnostic diagnostic = makeDiagnosticForRendering();
  const SourceSpan &primary = diagnostic.primarySpan()->get();
  const usize lineNumberWidth = std::to_string(primary.location.line()).size();
  const String locationPrefix = String(lineNumberWidth - 1, ' ') + "--> ";
  const String sourcePrefix = std::format("{:>{}} | ", primary.location.line(), lineNumberWidth);
  const String markerPrefix = String(lineNumberWidth + 1, ' ') + "| ";
  const SourceManager sources{Vec<Path>{std::filesystem::current_path()}};
  const String plain = renderToString(diagnostic,
      sources,
      RendererOptions{.color = ColorMode::Never, .terminal = false, .showSource = true, .tabWidth = 4});
  const String compact = renderToString(diagnostic,
      sources,
      RendererOptions{
          .color = ColorMode::Never,
          .terminal = false,
          .showSource = false,
          .details = DetailMode::None,
      });
  const String coloured = renderToString(diagnostic,
      sources,
      RendererOptions{.color = ColorMode::Always, .terminal = false, .showSource = true, .tabWidth = 4});

  Diagnostic custom = makeDiagnostic(DiagnosticCode::AssertionFailed);
  custom.header.descriptionOverride = "custom diagnostic description";

  check(diagnostic.code() == "NYX001"_exp);
  check(diagnostic.description() == "assertion failed"_exp);
  check(diagnosticDescription(DiagnosticCode::Unknown) == "Unknown"_exp);
  check(diagnosticCode(DiagnosticCode::Unknown) == "NYX000"_exp);
  check(custom.description() == "custom diagnostic description"_exp);
  check(plain.contains("error[NYX001]: assertion failed"));
  check(plain.contains("left value"));
  check(plain.contains("= note: left: 2"));
  check(plain.contains("= note: difference: 3"));
  check(plain.contains("const auto location"));
  check(plain.contains(locationPrefix));
  check(plain.contains(sourcePrefix));
  check(plain.contains(markerPrefix));
  check(plain.contains("attachment: input"));
  check(not plain.contains("\x1b["));
  check(compact.contains("error[NYX001]: assertion failed"));
  check(not compact.contains("left: 2"));
  check(not compact.contains("attachment: input"));
  check(coloured.contains("\x1b[1;31m"));
  check(coloured.contains("\x1b[1;31m3\x1b[0m"));
  check(coloured.contains("\x1b[0m"));
};

} // namespace diagnostics

namespace environment {

auto boundTo(TestEnvironment &expected) -> bool {
  const Option<Ref<TestEnvironment>> current = currentEnvironment();
  return current and std::addressof(current->get()) == std::addressof(expected);
}

[[= test]] auto bindingRestoresPreviousEnvironment() -> void {
  TestEnvironment outer{};
  TestEnvironment inner{};
  bool innerBound{};
  bool outerRestored{};

  {
    EnvironmentBinding outerBinding{outer};
    {
      EnvironmentBinding innerBinding{inner};
      innerBound = boundTo(inner);
    }
    outerRestored = boundTo(outer);
  }

  check(innerBound);
  check(outerRestored);
  check(currentEnvironment());
}

} // namespace environment

namespace assertions {

[[= test]] auto checksRecordDiagnostics() -> void {
  constexpr usize expectedAssertions{2};
  constexpr usize expectedFailures{1};
  const auto location = std::source_location::current();
  bool passed{};
  bool failed{};
  TestState state{};

  {
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};
    passed = check(true);
    failed = check(false, location);
    state = environment.state();
  }

  require(passed);
  require(not failed);
  require(state.assertions == expectedAssertions);
  require(state.failedAssertions == expectedFailures);
  require(state.failed());
  require(state.diagnostics.size() == expectedFailures);
  check(state.diagnostics.front().header.code == DiagnosticCode::AssertionFailed);
  check(state.diagnostics.front().details.spans.front().location.line() == location.line());
  check(state.diagnostics.front().details.spans.front().label == "assertion"_exp);
}

[[= test]] auto successfulRequirementsAllowContinuation() -> void {
  constexpr usize expectedAssertions{1};
  i32 value{};
  TestState state{};

  {
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};

    Result<i32> result{3};

    require(result);

    value = *result;
    state = environment.state();
  }

  check(value == 3);
  check(state.assertions == expectedAssertions);
  check(state.passed());
  check(state.diagnostics.empty());
}

[[= test]] auto failedRequirementsStopContinuation() -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedFailures{1};
  const auto location = std::source_location::current();

  bool aborted{};
  bool continued{};
  TestState state{};

  {
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};
    Result<i32> result{bail{Error{"unavailable"}}};
    try {
      require(result, location);
      continued = true;
    } catch (const TestAbort &) {
      aborted = true;
    }
    state = environment.state();
  }

  require(aborted);
  require(not continued);
  require(state.assertions == expectedAssertions);
  require(state.failedAssertions == expectedFailures);
  require(state.aborted);
  require(state.failed());
  require(state.diagnostics.size() == expectedFailures);
  check(state.diagnostics.front().description() == "requirement failed"_exp);
  check(state.diagnostics.front().details.spans.front().location.line() == location.line());
  check(state.diagnostics.front().details.spans.front().label == "requirement"_exp);
}

} // namespace assertions

namespace executions {

[[= test]] auto executesVoidTestsInAnEnvironment() -> void {
  const TestExecution execution = run("void", [] -> void { require(currentEnvironment()); });

  require(execution.passed());
  require(execution.state.assertions == 1_exp);
  require(currentEnvironment());
}

[[= test]] auto normalizesBooleanReturnValues() -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedFailures{1};
  const auto location = std::source_location::current();
  const TestExecution execution = run(
      TestDescriptor{
          .identifier = "returnsFalse",
          .location = location,
      },
      [] -> bool { return false; });

  require(execution.failed());
  require(execution.state.assertions == expectedAssertions);
  require(execution.state.failedAssertions == expectedFailures);
  require(execution.state.errors == 0_exp);
  require(execution.state.diagnostics.size() == expectedFailures);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::AssertionFailed);
  check(execution.state.diagnostics.front().description() == "test returned false"_exp);
  check(execution.state.diagnostics.front().details.spans.front().location.line() == location.line());
  check(execution.state.diagnostics.front().details.spans.front().label == "test return"_exp);
}

[[= test]] auto normalizesResultReturnValues() -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedFailures{1};
  constexpr usize expectedErrors{1};
  const TestExecution returnedTrue = run("resultTrue", [] -> Result<bool> { return true; });
  const TestExecution returnedFalse = run("resultFalse", [] -> Result<bool> { return false; });
  const TestExecution returnedVoid = run("resultVoid", [] -> Result<void> { return {}; });
  const TestExecution returnedError =
      run("resultError", [] -> Result<void> { return bail{Error{"invalid user"}}; });

  require(returnedTrue.passed());
  require(returnedTrue.state.assertions == expectedAssertions);
  require(returnedFalse.failed());
  require(returnedFalse.state.assertions == expectedAssertions);
  require(returnedFalse.state.failedAssertions == expectedFailures);
  require(returnedVoid.passed());
  require(returnedVoid.state.assertions == 0_exp);
  require(returnedError.failed());
  require(returnedError.state.errors == expectedErrors);
  require(returnedError.state.assertions == 0);
  require(returnedError.state.diagnostics.size() == expectedErrors);
  check(returnedError.state.diagnostics.front().header.code == DiagnosticCode::TestReturnedError);
  check(returnedError.state.diagnostics.front().details.notes.front().message == "invalid user"_exp);
}

[[= test]] auto capturesFatalRequirementsAtTheTestBoundary() -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedFailures{1};
  bool continued{};
  const TestExecution execution = run("requires", [&continued] -> void {
    require(false);
    continued = true;
  });

  require(not continued);
  require(execution.failed());
  require(execution.state.aborted);
  require(execution.state.assertions == expectedAssertions);
  require(execution.state.failedAssertions == expectedFailures);
  require(execution.state.errors == 0_exp);
  check(execution.state.diagnostics.front().details.spans.front().label == "requirement"_exp);
}

[[= test]] auto capturesUnhandledExceptions() -> void {
  constexpr usize expectedErrors{1};
  const TestExecution execution = run("throws", [] -> void { throw std::runtime_error{"broken test"}; });

  require(execution.failed());
  require(execution.state.errors == expectedErrors);
  require(execution.state.assertions == 0);
  require(execution.state.diagnostics.size() == expectedErrors);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::UnhandledException);
  check(execution.state.diagnostics.front().details.notes.front().message == "exception: broken test"_exp);
}

[[= test]] auto reportsAggregateResults() -> void {
  constexpr usize progressBarWidth{80};
  constexpr usize expectedTests{3};
  constexpr usize expectedPassed{1};
  constexpr usize expectedFailed{2};
  constexpr usize expectedAssertions{2};
  constexpr usize expectedFailedAssertions{1};
  constexpr usize expectedErrors{1};
  const Vec<TestExecution> executions{
      run("passes", [] -> bool { return true; }),
      run("fails", [] -> bool { return false; }),
      run("errors", [] -> Result<void> { return bail{Error{"invalid query"}}; }),
  };
  Reporter reporter{
      ReporterOptions{
          .renderer =
              RendererOptions{
                  .color = ColorMode::Never,
                  .terminal = false,
                  .showSource = false,
              },
      },
  };
  std::ostringstream output{};
  const TestSummary summary = reporter.report(executions, output);
  const String text = output.str();

  require(summary.testCount == expectedTests);
  require(summary.passedCount == expectedPassed);
  require(summary.failedCount == expectedFailed);
  require(summary.assertionCount == expectedAssertions);
  require(summary.failedAssertionCount == expectedFailedAssertions);
  require(summary.errorCount == expectedErrors);
  require(summary.failed());
  check(text.contains("test fails ... FAILED"));
  check(text.contains("error[NYX001]: test returned false"));
  check(text.contains("error[NYX011]: test returned an error"));
  check(text.contains("= note: test: errors"));
  check(text.contains(String(progressBarWidth, '=')));
  check(text.contains("test result: FAILED"));
  check(text.contains("finished in "));
}

} // namespace executions

namespace discovery {

[[= test]] auto discoversEachDeclarativeCase() -> void {
  constexpr usize expectedCases{4};
  const Vec<TestDescriptor> descriptors = discover<^^PassingTests>();

  require(eq(descriptors.size(), expectedCases));
  check(descriptors.front().identifier == "fibonacciCases(0, 0)"_exp);
  check(descriptors[1].identifier == "fibonacciCases(1, 1)"_exp);
  check(descriptors[2].identifier == "fibonacciCases(5, 5)"_exp);
  check(descriptors.back().identifier == "voidCase"_exp);
}

[[= test]] auto dispatchesParameterisedTests() -> void {
  constexpr usize expectedCases{4};
  constexpr usize expectedAssertions{4};
  const Vec<TestExecution> executions = runAll<^^PassingTests>();
  const TestSummary summary = Reporter::summarize(executions);

  check(executions.size() == expectedCases);
  check(summary.testCount == expectedCases);
  check(summary.passedCount == expectedCases);
  check(summary.assertionCount == expectedAssertions);
  check(summary.passed());
}

[[= test]] auto preservesCaseIdentityForFailures() -> void {
  constexpr usize expectedCases{2};
  constexpr usize expectedPassed{1};
  constexpr usize expectedFailed{1};
  const Vec<TestExecution> executions = runAll<^^FailingTests>();
  const TestSummary summary = Reporter::summarize(executions);

  require(executions.size() == expectedCases);
  require(summary.passedCount == expectedPassed);
  require(summary.failedCount == expectedFailed);
  require(executions.back().failed());
  check(executions.back().descriptor.identifier == "identityCases(3, 2)"_exp);
  check(executions.back().state.diagnostics.front().description() == "test returned false"_exp);
}

[[= test]] auto dispatchesIndependentCasesInParallel() -> void {
  constexpr usize workerCount{2};
  constexpr usize expectedCases{4};
  ParallelTests::reset();
  const Vec<TestExecution> executions = runAll<^^ParallelTests>(RunOptions{
      .jobs = workerCount,
  });
  const TestSummary summary = Reporter::summarize(executions);

  require(executions.size() == expectedCases);
  require(summary.passedCount == expectedCases);
  check(ParallelTests::peak.load(std::memory_order_relaxed) >= workerCount);
  check(executions.front().descriptor.identifier == "runsConcurrently(1)");
  check(executions.back().descriptor.identifier == "runsConcurrently(4)");
}

} // namespace discovery

namespace expressions {

auto containsHighlighted(const Vec<DiagnosticFragment> &fragments, StringView text) -> bool {
  return std::ranges::any_of(fragments, [text](const DiagnosticFragment &fragment) -> bool {
    return fragment.highlighted and fragment.text == text;
  });
}

[[= test]] auto compactLiteralsUseTheAssertionLocation() -> void {
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

[[= test]] auto literalOperatorsProduceExpressions() -> void {
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

[[= test]] auto stringDifferencesVisualizeWhitespace() -> void {
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

[[= test]] auto utilityComparatorsProduceExpressions() -> void {
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

[[= test]] auto requiredExpressionsAbortTheTest() -> void {
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

[[= test]] auto returnedExpressionsPreserveTheirDiagnostics() -> void {
  const TestExecution execution = run("returnsExpression", [] -> Expression { return eq(u32{2}, 3_exp); });

  require(execution.failed());
  require(execution.state.failedAssertions == 1_exp);
  require(execution.state.errors == 0_exp);
  check(execution.state.diagnostics.front().description() == "assertion failed"_exp);
  check(execution.state.diagnostics.front().details.notes.front().message ==
        "condition: values are not equal"_exp);
}

[[= test]] auto comparisonsExposeValuesAndLocations() -> void {
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

} // namespace expressions

namespace fixtures {

namespace FixtureSubjects {

struct Transient final {
  u32 instance{};
};

struct Shared final {
  u32 instance{};
};

struct PerTest final {
  usize instance{};
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline usize transientCreations{};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline usize sharedCreations{};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline usize perTestCreations{};

auto resetFixtureCounters() -> void {
  transientCreations = 0;
  sharedCreations = 0;
  perTestCreations = 0;
}

[[= fixture]] auto transient() -> Transient {
  const Option<Ref<const Context>> context = currentContext();
  ++transientCreations;
  return Transient{
      .instance = context ? static_cast<u32>(context->get().testCase + 1) : 0,
  };
}

[[ = fixture, = once ]] auto shared() -> Shared {
  ++sharedCreations;
  return Shared{
      .instance = 1,
  };
}

[[= fixture]] auto perTest() -> PerTest {
  return PerTest{
      .instance = ++perTestCreations,
  };
}

[[
  = test,
  = description<"injects a context and both fixture lifetimes">(),
  = Case{11},
  = Case{29},
  = arg<"ctx">(context),
  = arg<"input">(fromCase)
]] auto receivesContext(Context ctx, u32 input, Transient transientValue, const Shared &sharedValue) -> void {
  const Option<Ref<const Context>> active = currentContext();
  const u32 expectedInput = ctx.testCase == 0 ? 11 : 29;

  require(active);
  require(ctx.name == "receivesContext"_exp);
  require(ctx.description == "injects a context and both fixture lifetimes"_exp);
  require(ctx.testCase < 2_exp);
  require(eq(input, expectedInput));
  require(eq(transientValue.instance, static_cast<u32>(ctx.testCase + 1)));
  require(sharedValue.instance == 1_exp);
  check(active->get().name == ctx.name);
  check(active->get().description == ctx.description);
  check(active->get().testCase == ctx.testCase);
}

[[= test]] auto reusesNormalFixtureWithinOneTest(PerTest first, const PerTest &second) -> void {
  require(eq(first.instance, second.instance));
}

[[ = test,
  = Case{11},
  = Case{29},
  = arg<"ctx">(context),
  = arg<"input">(fromCase) ]] auto receivesAsyncContext(Context ctx,
    u32 input,
    const Transient &transientValue,           // NOLINT
    const Shared &sharedValue) -> Task<void> { // NOLINT
  co_await yield();

  const Option<Ref<const Context>> active = currentContext();
  const u32 expectedInput = ctx.testCase == 0 ? 11 : 29;

  require(active);
  require(ctx.name == "receivesAsyncContext"_exp);
  require(eq(input, expectedInput));
  require(eq(transientValue.instance, static_cast<u32>(ctx.testCase + 1)));
  require(sharedValue.instance == 1_exp);
  check(active->get().testCase == ctx.testCase);
}

} // namespace FixtureSubjects

[[= test]] auto directRunInjectsContext() -> void {
  const Option<Ref<const Context>> outer = currentContext();
  const auto location = std::source_location::current();
  const TestExecution execution = run(
      TestDescriptor{
          .identifier = "directContext",
          .location = location,
      },
      [location](const Context &context) -> void {
        const Option<Ref<const Context>> active = currentContext();

        require(context.name == "directContext"_exp);
        require(context.description.empty());
        require(context.testCase == 0_exp);
        require(context.location.line() == location.line());
        require(active);
        check(std::addressof(active->get()) == std::addressof(context));
      });

  require(outer);
  require(execution.passed());
  require(execution.descriptor.name == "directContext"_exp);
  require(execution.state.assertions == 6_exp);
  require(currentContext());
  check(currentContext()->get().name == outer->get().name);
}

[[= test]] auto reflectedInjectionUsesFixtureScopes() -> void {
  FixtureSubjects::resetFixtureCounters();

  const Vec<TestExecution> firstRun = runAll<^^FixtureSubjects>();
  const Vec<TestExecution> secondRun = runAll<^^FixtureSubjects>();
  const auto passed = [](const TestExecution &execution) -> bool { return execution.passed(); };

  require(firstRun.size() == 5_exp);
  require(secondRun.size() == 5_exp);
  require(std::ranges::all_of(firstRun, passed));
  require(std::ranges::all_of(secondRun, passed));
  require(FixtureSubjects::transientCreations == 8_exp);
  require(FixtureSubjects::sharedCreations == 2_exp);
  require(FixtureSubjects::perTestCreations == 2_exp);
  check(firstRun.front().descriptor.name == "receivesContext"_exp);
  check(firstRun.front().descriptor.description == "injects a context and both fixture lifetimes"_exp);
  check(firstRun.front().descriptor.testCase == 0_exp);
  check(firstRun[1].descriptor.testCase == 1_exp);
  check(firstRun[2].descriptor.name == "reusesNormalFixtureWithinOneTest"_exp);
  check(firstRun[3].descriptor.name == "receivesAsyncContext"_exp);
}

} // namespace fixtures

namespace providers {

namespace ProviderSubjects {

[[ = test, = arg<"input">(values(None, " ", "    ")) ]] auto optionalValues(Option<const char *> input)
    -> void {
  if (input)
    require(StringView{*input}.find(' ') == 0_exp);
  else
    require(not input);
}

[[ = test,
  = Case{2},
  = Case{5},
  = arg<"base">(fromCase),
  = arg<"offset">(values(10, 20)) ]] auto caseAndValues(u32 base, u32 offset) -> void {
  require(base > 0_exp);
  require(offset == 10_exp or offset == 20_exp);
}

[[ = test, = arg<"ctx">(context), = arg<"value">(values(3, 7)) ]] auto receivesProviderContext(Context ctx,
    u32 value) -> void {
  require(ctx.testCase < 2_exp);
  require(value == 3_exp or value == 7_exp);
}

[[ = test, = arg<"path">(files<__FILE__>()) ]] auto receivesFile(const Path &path) -> void {
  require(path == Path{__FILE__});
}

} // namespace ProviderSubject

auto temporaryDirectory() -> Path {
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() / std::format("nyx-test-provider-{}", tick);
}

auto writeFile(const Path &path) -> void {
  std::ofstream output{path};
  require(output);
  output << "Nyx.Test provider fixture\n";
  require(output);
}

[[nodiscard]] auto executionNamed(const Vec<TestExecution> &executions, StringView identifier)
    -> Option<Ref<const TestExecution>> {
  const auto execution = std::ranges::find_if(
      executions, [identifier](const TestExecution &candidate) constexpr noexcept -> bool {
        return candidate.descriptor.identifier == identifier;
      });
  if (execution == executions.end())
    return None;

  return std::cref(*execution);
}

[[= test]] auto expandsValuesAndFileProviders() -> void {
  constexpr usize expectedDescriptors{11};
  constexpr usize expectedPassed{10};
  constexpr usize expectedFailed{1};
  Vec<TestDescriptor> descriptors = discover<^^ProviderSubjects>();
  discover<^^FailingProviderSubjects>(descriptors);
  Vec<detail::WorkItem> workItems{};
  runAll<^^ProviderSubjects>(workItems);
  runAll<^^FailingProviderSubjects>(workItems);
  Vec<TestExecution> executions = detail::executeWorkItems(std::move(workItems), {});
  const TestSummary summary = Reporter::summarize(executions);

  const Option<Ref<const TestExecution>> noFiles =
      executionNamed(executions, "receivesNoFiles(path=<no values>)");
  const Option<Ref<const TestExecution>> firstContext =
      executionNamed(executions, "receivesProviderContext(value=3)");
  const Option<Ref<const TestExecution>> secondContext =
      executionNamed(executions, "receivesProviderContext(value=7)");

  require(eq(descriptors.size(), expectedDescriptors));
  require(eq(executions.size(), expectedDescriptors));
  require(eq(summary.passedCount, expectedPassed));
  require(eq(summary.failedCount, expectedFailed));
  require(noFiles);
  require(firstContext);
  require(secondContext);
  check(descriptors.front().identifier == "optionalValues(input=None)"_exp);
  check(descriptors[1].identifier == "optionalValues(input=\" \")"_exp);
  check(descriptors[2].identifier == "optionalValues(input=\"    \")"_exp);
  check(descriptors[3].identifier == "caseAndValues(2, offset=10)"_exp);
  check(descriptors[6].identifier == "caseAndValues(5, offset=20)"_exp);
  check(noFiles->get().failed());
  check(noFiles->get().state.errors == 1_exp);
  check(noFiles->get().state.diagnostics.front().header.code == DiagnosticCode::ProviderProducedNoValues);
  check(firstContext->get().descriptor.testCase == 0_exp);
  check(secondContext->get().descriptor.testCase == 1_exp);
}

[[= test]] auto matchesAndFiltersFiles() -> void {
  const Path root = temporaryDirectory();
  const auto cleanup = std::scope_exit([&root] -> void {
    std::error_code error;
    std::filesystem::remove_all(root, error);
  });
  std::error_code error;
  std::filesystem::create_directories(root / ".hidden", error);
  require(not error);
  writeFile(root / "included.md");
  writeFile(root / "excluded.md");
  writeFile(root / ".hidden" / "hidden.md");
  writeFile(root / "ignored.txt");

  const String pattern = (root / "**/*.md").generic_string();
  const FileQuery visible{
      .pattern = pattern,
      .excludes = Vec<String>{"excluded"},
  };
  const FileQuery all{
      .pattern = pattern,
      .includeDotFiles = true,
  };
  const FileQuery hiddenLiteral{
      .pattern = (root / ".hidden" / "hidden.md").generic_string(),
  };
  const FileQuery visibleHiddenLiteral{
      .pattern = hiddenLiteral.pattern,
      .includeDotFiles = true,
  };
  const Vec<Path> visibleFiles = findFiles(visible);
  const Vec<Path> allFiles = findFiles(all);
  const Vec<Path> hiddenFiles = findFiles(hiddenLiteral);
  const Vec<Path> visibleHiddenFiles = findFiles(visibleHiddenLiteral);

  require(matchesGlob("src/readme.md", "src/**/*.md"));
  require(matchesGlob("src/docs/readme.md", "src/**/*.md"));
  require(not matchesGlob("src/docs/readme.txt", "src/**/*.md"));
  require(visibleFiles.size() == 1_exp);
  require(allFiles.size() == 3_exp);
  require(hiddenFiles.empty());
  require(visibleHiddenFiles.size() == 1_exp);
  check(visibleFiles.front().filename().generic_string() == "included.md"_exp);
  check(std::ranges::any_of(
      allFiles, [](const Path &path) -> bool { return path.filename().generic_string() == "hidden.md"; }));
}

} // namespace providers

namespace coroutines {

auto delayedValue() -> Task<i32> {
  constexpr i32 value{69};
  co_await yield();
  co_return value;
}

[[= test]] auto executesVoidTestsInAnEnvironment() -> void {
  const TestExecution execution = run("void", [] -> void { require(currentEnvironment()); });

  require(execution.passed());
  require(execution.state.assertions == 1);
  require(currentEnvironment());
}

[[= test]] auto normalizesBooleanReturnValues() -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedFailures{1};
  const auto location = std::source_location::current();
  const TestExecution execution = run(
      TestDescriptor{
          .identifier = "returnsFalse",
          .location = location,
      },
      [] -> bool { return false; });

  require(execution.failed());
  require(execution.state.assertions == expectedAssertions);
  require(execution.state.failedAssertions == expectedFailures);
  require(execution.state.errors == 0);
  require(execution.state.diagnostics.size() == expectedFailures);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::AssertionFailed);
  check(execution.state.diagnostics.front().description() == "test returned false");
  check(execution.state.diagnostics.front().details.spans.front().location.line() == location.line());
  check(execution.state.diagnostics.front().details.spans.front().label == "test return");
}

[[= test]] auto normalizesResultReturnValues() -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedFailures{1};
  constexpr usize expectedErrors{1};
  const TestExecution returnedTrue = run("resultTrue", [] -> Result<bool> { return true; });
  const TestExecution returnedFalse = run("resultFalse", [] -> Result<bool> { return false; });
  const TestExecution returnedVoid = run("resultVoid", [] -> Result<void> { return {}; });
  const TestExecution returnedError =
      run("resultError", [] -> Result<void> { return bail{Error{"invalid user"}}; });

  require(returnedTrue.passed());
  require(returnedTrue.state.assertions == expectedAssertions);
  require(returnedFalse.failed());
  require(returnedFalse.state.assertions == expectedAssertions);
  require(returnedFalse.state.failedAssertions == expectedFailures);
  require(returnedVoid.passed());
  require(returnedVoid.state.assertions == 0);
  require(returnedError.failed());
  require(returnedError.state.errors == expectedErrors);
  require(returnedError.state.assertions == 0);
  require(returnedError.state.diagnostics.size() == expectedErrors);
  check(returnedError.state.diagnostics.front().header.code == DiagnosticCode::TestReturnedError);
  check(returnedError.state.diagnostics.front().details.notes.front().message == "invalid user");
}

[[= test]] auto capturesFatalRequirementsAtTheTestBoundary() -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedFailures{1};
  bool continued{};
  const TestExecution execution = run("requires", [&continued] -> void {
    require(false);
    continued = true;
  });

  require(not continued);
  require(execution.failed());
  require(execution.state.aborted);
  require(execution.state.assertions == expectedAssertions);
  require(execution.state.failedAssertions == expectedFailures);
  require(execution.state.errors == 0);
  check(execution.state.diagnostics.front().details.spans.front().label == "requirement");
}

[[= test]] auto preservesTaskLocalBindingsAcrossAsyncResumption() -> void {
  constexpr usize expectedAssertions{4};
  bool completed{};
  const TestExecution execution = run("asyncBindings", [&completed] -> Task<void> { // NOLINT
    require(currentEnvironment());
    require(currentContext());

    co_await yield();

    require(currentEnvironment());
    require(currentContext());
    completed = true;
  });

  require(completed);
  require(execution.passed());
  require(execution.state.assertions == expectedAssertions);
}

[[= test]] auto awaitsNestedAsyncTasks() -> void {
  constexpr usize expectedAssertions{1};
  const TestExecution execution = run("nestedAsync", [] -> Task<bool> {
    constexpr i32 expectedValue{69};
    const i32 value = co_await delayedValue();
    co_return value == expectedValue;
  });

  require(execution.passed());
  require(execution.state.assertions == expectedAssertions);
  require(execution.state.diagnostics.empty());
}

[[= test]] auto normalizesAsyncReturnValues() -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedErrors{1};
  const TestExecution returnedTrue = run("asyncTrue", [] -> Task<bool> {
    co_await yield();
    co_return true;
  });
  const TestExecution returnedResult = run("asyncResult", [] -> Task<Result<bool>> {
    co_await yield();
    co_return Result<bool>{true};
  });
  const TestExecution returnedError = run("asyncError", [] -> Task<Result<void>> {
    co_await yield();
    Result<void> result = bail{Error{"async failure"}};
    co_return result;
  });

  require(returnedTrue.passed());
  require(returnedTrue.state.assertions == expectedAssertions);
  require(returnedResult.passed());
  require(returnedResult.state.assertions == expectedAssertions);
  require(returnedError.failed());
  require(returnedError.state.errors == expectedErrors);
  check(returnedError.state.diagnostics.front().header.code == DiagnosticCode::TestReturnedError);
  check(returnedError.state.diagnostics.front().details.notes.front().message == "async failure");
}

[[= test]] auto capturesAsyncFatalRequirementsAtTheTestBoundary() -> void {
  constexpr usize expectedAssertions{1};
  constexpr usize expectedFailures{1};
  bool continued{};
  const TestExecution execution = run("asyncRequire", [&continued] -> Task<void> { // NOLINT
    co_await yield();
    require(false);
    continued = true;
  });

  require(not continued);
  require(execution.failed());
  require(execution.state.aborted);
  require(execution.state.assertions == expectedAssertions);
  require(execution.state.failedAssertions == expectedFailures);
  require(execution.state.errors == 0);
  check(execution.state.diagnostics.front().details.spans.front().label == "requirement");
}

[[= test]] auto capturesAsyncExceptionsAtTheTestBoundary() -> void {
  constexpr usize expectedErrors{1};
  const TestExecution execution = run("asyncThrows", [] -> Task<void> {
    co_await yield();
    throw std::runtime_error{"broken async test"};
  });

  require(execution.failed());
  require(execution.state.errors == expectedErrors);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::UnhandledException);
  check(execution.state.diagnostics.front().details.notes.front().message == "exception: broken async test");
}

[[= test]] auto capturesUnhandledExceptions() -> void {
  constexpr usize expectedErrors{1};
  const TestExecution execution = run("throws", [] -> void { throw std::runtime_error{"broken test"}; });

  require(execution.failed());
  require(execution.state.errors == expectedErrors);
  require(execution.state.assertions == 0);
  require(execution.state.diagnostics.size() == expectedErrors);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::UnhandledException);
  check(execution.state.diagnostics.front().details.notes.front().message == "exception: broken test");
}

[[= test]] auto reportsAggregateResults() -> void {
  constexpr usize progressBarWidth{80};
  constexpr usize expectedTests{3};
  constexpr usize expectedPassed{1};
  constexpr usize expectedFailed{2};
  constexpr usize expectedAssertions{2};
  constexpr usize expectedFailedAssertions{1};
  constexpr usize expectedErrors{1};
  const Vec<TestExecution> executions{
      run("passes", [] -> bool { return true; }),
      run("fails", [] -> bool { return false; }),
      run("errors", [] -> Result<void> { return bail{Error{"invalid query"}}; }),
  };
  Reporter reporter{
      ReporterOptions{
          .renderer =
              RendererOptions{
                  .color = ColorMode::Never,
                  .terminal = false,
                  .showSource = false,
              },
      },
  };
  std::ostringstream output{};
  const TestSummary summary = reporter.report(executions, output);
  const String text = output.str();

  require(summary.testCount == expectedTests);
  require(summary.passedCount == expectedPassed);
  require(summary.failedCount == expectedFailed);
  require(summary.assertionCount == expectedAssertions);
  require(summary.failedAssertionCount == expectedFailedAssertions);
  require(summary.errorCount == expectedErrors);
  require(summary.failed());
  check(text.contains("test fails ... FAILED"));
  check(text.contains("error[NYX001]: test returned false"));
  check(text.contains("error[NYX011]: test returned an error"));
  check(text.contains("= note: test: errors"));
  check(text.contains(String(progressBarWidth, '=')));
  check(text.contains("test result: FAILED"));
  check(text.contains("finished in "));
}

} // namespace coroutines

} // namespace Tests

auto main() -> int { // NOLINT
  const Vec<TestExecution> executions = runAll<^^Tests>();
  Reporter reporter{
      ReporterOptions{
          .renderer =
              RendererOptions{
                  .color = Nyx::Test::ColorMode::Always,
                  .terminal = true,
                  .showSource = true,
                  .details = DetailMode::Trace,
              },
          .showPassedTests = true,
          .showSummary = true,
      },
  };
  reporter.report(executions, std::cout);
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
