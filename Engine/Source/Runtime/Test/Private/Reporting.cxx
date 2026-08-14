module Nyx.Test;

import :Diagnostics;
import :Execution;
import :Render;
import :Reporting;

import std;
import Nyx.Core;

namespace Nyx::Test {

namespace {

constexpr inline StringView green = "\x1b[1;32m";
constexpr inline StringView red = "\x1b[1;31m";
constexpr inline StringView cyan = "\x1b[1;36m";
constexpr inline StringView reset = "\x1b[1;0m";
constexpr inline usize progressBarWidth{80};

[[nodiscard]] constexpr auto paint(StringView text, StringView color, bool useColor) -> String {
  if (not useColor)
    return String{text};

  String result{};
  result.reserve(color.size() + text.size() + reset.size());
  result.append(color);
  result.append(text);
  result.append(reset);
  return result;
}

[[nodiscard]] constexpr auto countLabel(usize count, StringView singular, StringView plural) -> String {
  return std::format("{} {}", count, count == 1 ? singular : plural);
}

[[nodiscard]] constexpr auto durationLabel(std::chrono::steady_clock::duration duration) -> String {
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
  if (milliseconds.count() > 0)
    return std::format("{} ms", milliseconds.count());

  const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
  return std::format("{} μs", microseconds.count());
}

[[nodiscard]] auto withTestContext(StringView identifier, const Diagnostic &diagnostic) -> Diagnostic {
  Diagnostic result = diagnostic;
  result.addNote(std::format("test: {}", identifier));
  return result;
}

[[nodiscard]] auto normalizePath(const Path &path) -> String {
  String res = path.generic_string();

  if (path.is_relative())
    return res;

  std::error_code error{};
  Path cwd = std::filesystem::current_path(error);
  if (error)
    return res;
  String cwdstr = cwd.generic_string();

  usize pos = res.find(cwdstr);

  if (pos == String::npos)
    return res;

  res.replace(res.begin(), res.begin() + static_cast<String::difference_type>(cwdstr.size() + 1), "");

  return res;
}

[[nodiscard]] constexpr auto hasTimeout(const TestExecution &execution) noexcept -> bool {
  const bool hasTimeoutDiagnostic = std::ranges::any_of(
      execution.state.diagnostics, [](const Diagnostic &diagnostic) constexpr noexcept -> bool {
        return diagnostic.header.code == DiagnosticCode::TimeoutExceeded;
      });
  if (not hasTimeoutDiagnostic)
    return false;

  return std::ranges::all_of(
      execution.state.diagnostics, [](const Diagnostic &diagnostic) constexpr noexcept -> bool {
        return diagnostic.header.code == DiagnosticCode::TimeoutExceeded;
      });
}

[[nodiscard]] constexpr auto sameSample(const TestAttempt &left, const TestAttempt &right) noexcept -> bool {
  return not left.warmup and not right.warmup and left.index.runIteration == right.index.runIteration and
         left.index.sample == right.index.sample;
}

[[nodiscard]] constexpr auto recoveredBy(const TestAttempt &failed, const Vec<TestAttempt> &attempts) noexcept
    -> bool {
  return hasTimeout(failed.execution) and
         std::ranges::any_of(attempts, [&failed](const TestAttempt &candidate) constexpr noexcept -> bool {
           return sameSample(failed, candidate) and candidate.index.retry > failed.index.retry and
                  candidate.execution.passed();
         });
}

[[nodiscard]] auto finalSamples(const Vec<TestAttempt> &attempts) -> Vec<const TestAttempt *> {
  Vec<const TestAttempt *> samples =
      attempts | std::views::filter([](const TestAttempt &attempt) constexpr noexcept -> bool {
        return not attempt.warmup;
      }) |
      std::views::transform(
          [](const TestAttempt &attempt) -> const TestAttempt * { return std::addressof(attempt); }) |
      std::ranges::to<Vec<const TestAttempt *>>();
  std::ranges::sort(
      samples, [](const TestAttempt *left, const TestAttempt *right) constexpr noexcept -> bool {
        if (left->index.runIteration != right->index.runIteration)
          return left->index.runIteration < right->index.runIteration;
        if (left->index.sample != right->index.sample)
          return left->index.sample < right->index.sample;
        return left->index.retry < right->index.retry;
      });

  Vec<const TestAttempt *> result{};
  result.reserve(samples.size());
  std::ranges::for_each(samples, [&result](const TestAttempt *attempt) -> void {
    if (result.empty() or result.back()->index.runIteration != attempt->index.runIteration or
        result.back()->index.sample != attempt->index.sample) {
      result.push_back(attempt);
      return;
    }

    result.back() = attempt;
  });
  return result;
}

[[nodiscard]] auto makeMeasurement(const TestCaseResult &testCase) -> Option<MeasurementSummary> {
  if (testCase.descriptor.policy.repeat <= 1 and testCase.descriptor.policy.warmup == 0)
    return None;

  const Vec<const TestAttempt *> samples = finalSamples(testCase.attempts);
  if (samples.empty())
    return MeasurementSummary{};

  Vec<std::chrono::steady_clock::duration> values =
      samples | std::views::transform([](const TestAttempt *attempt) -> std::chrono::steady_clock::duration {
        return attempt->execution.duration;
      }) |
      std::ranges::to<Vec<std::chrono::steady_clock::duration>>();
  return detail::summarizeMeasurements(std::move(values));
}

[[nodiscard]] auto attemptLabel(const TestExecution &execution) -> String {
  if (execution.warmup)
    return std::format("warmup {}", execution.attempt.sample + 1);

  if (execution.attempt.retry == 0 and execution.attempt.sample == 0 and execution.iteration == 0)
    return {};

  return std::format("sample {}, retry {}, run {}",
      execution.attempt.sample + 1,
      execution.attempt.retry,
      execution.attempt.runIteration + 1);
}

[[nodiscard]] auto durationText(std::chrono::steady_clock::duration duration) -> String {
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
  if (milliseconds.count() != 0)
    return std::format("{} ms", milliseconds.count());

  const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
  return std::format("{} μs", microseconds.count());
}

} // namespace

struct Reporter::RenderState final {
  bool previousFailure{};
};

auto TestSummary::passed() const noexcept -> bool {
  return failedCaseCount == 0 and (caseCount != 0 or failedCount == 0);
}

auto TestSummary::failed() const noexcept -> bool {
  return not passed();
}

auto TestCaseResult::passed() const noexcept -> bool {
  return std::ranges::all_of(attempts, [this](const TestAttempt &attempt) constexpr noexcept -> bool {
    return attempt.execution.passed() or (not attempt.warmup and recoveredBy(attempt, attempts));
  });
}

auto TestCaseResult::failed() const noexcept -> bool {
  return not passed();
}

auto RunReport::passed() const noexcept -> bool {
  return std::ranges::all_of(
      cases, [](const TestCaseResult &result) constexpr noexcept -> bool { return result.passed(); });
}

auto RunReport::failed() const noexcept -> bool {
  return not passed();
}

Reporter::Reporter(ReporterOptions options)
    : options_(options) {
}

auto Reporter::addRoot(Path root) -> void {
  if constexpr (build::tests)
    roots_.push_back(std::move(root));
}

auto Reporter::makeReport(Span<const TestExecution> executions) -> RunReport {
  if constexpr (not build::tests)
    return {};

  RunReport result{};
  result.cases.reserve(executions.size());

  std::ranges::for_each(executions, [&result](const TestExecution &execution) -> void {
    const auto existing = std::ranges::find_if(
        result.cases, [&execution](const TestCaseResult &testCase) constexpr noexcept -> bool {
          return testCase.descriptor.identifier == execution.descriptor.identifier;
        });

    const TestAttempt attempt{
        .execution = execution,
        .index = execution.attempt,
        .warmup = execution.warmup,
    };
    if (existing == result.cases.end()) {
      result.cases.push_back(TestCaseResult{
          .descriptor = execution.descriptor,
          .attempts = Vec<TestAttempt>{attempt},
      });
      return;
    }

    existing->attempts.push_back(attempt);
  });

  std::ranges::for_each(result.cases, [](TestCaseResult &testCase) -> void {
    testCase.measurement = makeMeasurement(testCase);
    testCase.recoveredTimeouts = static_cast<usize>(
        std::ranges::count_if(testCase.attempts, [&testCase](const TestAttempt &attempt) -> bool {
          return not attempt.warmup and not attempt.execution.passed() and
                 recoveredBy(attempt, testCase.attempts);
        }));
  });

  return result;
}

auto Reporter::summarize(Span<const TestExecution> executions) noexcept -> TestSummary {
  if constexpr (not build::tests)
    return {};

  return summarize(makeReport(executions));
}

auto Reporter::summarize(const RunReport &report) noexcept -> TestSummary {
  if constexpr (not build::tests)
    return {};

  TestSummary result{
      .caseCount = report.cases.size(),
  };

  std::ranges::for_each(report.cases, [&result](const TestCaseResult &testCase) constexpr -> void {
    if (testCase.passed())
      ++result.passedCaseCount;
    else
      ++result.failedCaseCount;

    if (testCase.recoveredTimeouts != 0)
      ++result.recoveredCount;

    std::ranges::for_each(testCase.attempts, [&result](const TestAttempt &attempt) constexpr -> void {
      ++result.testCount;
      ++result.attemptCount;
      result.duration += attempt.execution.duration;
      result.wallDuration += attempt.execution.wallDuration;
      result.assertionCount += attempt.execution.state.assertions;
      result.failedAssertionCount += attempt.execution.state.failedAssertions;
      result.errorCount += attempt.execution.state.errors;

      if (attempt.warmup)
        ++result.warmupCount;
      else
        ++result.sampleCount;
      if (attempt.index.retry != 0)
        ++result.retryCount;

      if (attempt.execution.passed())
        ++result.passedCount;
      else
        ++result.failedCount;
      ;
    });
  });

  return result;
}

auto Reporter::colorEnabled() const noexcept -> bool {
  switch (options_.renderer.color) {
    case ColorMode::Always: return true;
    case ColorMode::Automatic: return options_.renderer.terminal;
    case ColorMode::Never:
    default: return false;
  }

  std::unreachable();
}

auto Reporter::shouldRenderTrace(const TestExecution &execution) const noexcept -> bool {
  if (execution.state.traces.empty())
    return false;

  if (has(options_.renderer.effectiveSections(), DiagnosticSection::Trace) or
      execution.traceMode == TraceMode::ForcedAll)
    return true;

  return execution.traceMode == TraceMode::ForcedFailures and execution.failed();
}

auto Reporter::renderFailure(const TestExecution &execution,
    const SourceManager &sources,
    std::ostream &output) const -> void {
  std::ranges::for_each(execution.state.diagnostics, [&](const Diagnostic &diagnostic) -> void {
    render(withTestContext(execution.descriptor.identifier, diagnostic), sources, output, options_.renderer);
  });
}

auto Reporter::renderTrace(const TestExecution &execution, std::ostream &output) const -> void {
  const bool useColor = colorEnabled();
  std::ranges::for_each(execution.state.traces, [&](const TraceEvent &event) -> void {
    output << paint("  = trace:", cyan, useColor) << ' ' << event.message;
    if (event.remoteLocation) {
      output << std::format(
          " ({}:{})", normalizePath(event.remoteLocation->file), event.remoteLocation->line);
    } else {
      output << std::format(" ({}:{})", normalizePath(event.location.file_name()), event.location.line());
    }

    output << '\n';
  });
}

auto Reporter::renderProfile(const TestExecution &execution, std::ostream &output) const -> void {
  if (not has(options_.renderer.effectiveSections(), DiagnosticSection::Profile))
    return;

  std::ranges::for_each(execution.profile.events, [&output](const profiling::ProfileEvent &event) -> void {
    output << std::format("  = profile: {} ({})\n", event.name, durationText(event.duration));
  });
}

auto Reporter::renderMeasurement(const TestCaseResult &testCase, std::ostream &output) const -> void {
  const bool useColor = colorEnabled();
  const MeasurementSummary &measurement = testCase.measurement.value_or(MeasurementSummary{});
  const bool passed = testCase.passed();
  const String status = String{passed ? "ok" : "FAILED"};
  output << std::format("tests {} ... {} {} in {} | min {}; max {}; mean {}; median {}; deviation {}\n",
      testCase.descriptor.identifier,
      paint(status, passed ? green : red, useColor),
      countLabel(measurement.sampleCount, "sample", "samples"),
      durationLabel(measurement.total),
      durationLabel(measurement.minimum),
      durationLabel(measurement.maximum),
      durationLabel(measurement.mean),
      durationLabel(measurement.median),
      durationLabel(measurement.deviation));
}

auto Reporter::renderMeasuredCase(const TestCaseResult &testCase,
    const SourceManager &sources,
    std::ostream &output,
    RenderState &state) const -> void {
  renderMeasurement(testCase, output);

  const auto representative =
      std::ranges::find_if(testCase.attempts, [this](const TestAttempt &attempt) -> bool {
        return not attempt.warmup and shouldRenderTrace(attempt.execution);
      });
  if (representative != testCase.attempts.end())
    renderTrace(representative->execution, output);

  std::ranges::for_each(testCase.attempts, [&](const TestAttempt &attempt) -> void {
    if (not attempt.execution.failed() or recoveredBy(attempt, testCase.attempts))
      return;

    output << '\n';
    if (shouldRenderTrace(attempt.execution))
      renderTrace(attempt.execution, output);
    renderProfile(attempt.execution, output);
    renderFailure(attempt.execution, sources, output);
    state.previousFailure = true;
  });
}

auto Reporter::renderAttempt(const TestAttempt &attempt,
    const SourceManager &sources,
    std::ostream &output,
    bool useColor,
    RenderState &state) const -> void {
  const TestExecution &execution = attempt.execution;
  if (execution.warmup and execution.passed())
    return;

  if (execution.warmup) {
    if (state.previousFailure)
      output << '\n';

    output << std::format("test {} ({}) ... {} {}\n",
        execution.descriptor.identifier,
        attemptLabel(execution),
        paint("FAILED", red, useColor),
        durationLabel(execution.duration));
    if (shouldRenderTrace(execution))
      renderTrace(execution, output);
    renderProfile(execution, output);
    renderFailure(execution, sources, output);
    state.previousFailure = true;
    return;
  }

  const bool passed = execution.passed();
  const bool showTrace = shouldRenderTrace(execution);
  if (passed and not options_.showPassedTests and not showTrace and execution.attempt.retry == 0)
    return;

  if (state.previousFailure)
    output << '\n';

  const String label = attemptLabel(execution);
  const String status = passed and execution.attempt.retry != 0
                            ? std::format("passed after {} timeout {}",
                                  execution.attempt.retry,
                                  countLabel(execution.attempt.retry, "retry", "retries"))
                            : String{passed ? "ok" : "FAILED"};
  output << std::format("test {}{} ... {} {}\n",
      execution.descriptor.identifier,
      label.empty() ? String{} : std::format(" ({}) ", label),
      paint(status, passed ? green : red, useColor),
      durationLabel(execution.duration));

  if (passed) {
    if (showTrace)
      renderTrace(execution, output);
    renderProfile(execution, output);

    state.previousFailure = false;
    return;
  }

  output << '\n';
  if (showTrace)
    renderTrace(execution, output);
  renderProfile(execution, output);
  renderFailure(execution, sources, output);
  state.previousFailure = true;
}

auto Reporter::renderCase(const TestCaseResult &testCase,
    const SourceManager &sources,
    std::ostream &output,
    bool useColor,
    RenderState &state) const -> void {
  if (testCase.measurement and testCase.attempts.size() > 1) {
    renderMeasuredCase(testCase, sources, output, state);
    return;
  }

  std::ranges::for_each(
      testCase.attempts, [this, &sources, &output, useColor, &state](const TestAttempt &attempt) -> void {
        renderAttempt(attempt, sources, output, useColor, state);
      });
}

auto Reporter::report(Span<const TestExecution> executions, std::ostream &output) const -> TestSummary {
  if constexpr (build::tests) {
    const bool useColor = colorEnabled();
    const SourceManager sources{roots_};
    const RunReport reportModel = makeReport(executions);
    const TestSummary summary = summarize(reportModel);
    RenderState state{};
    std::ranges::for_each(reportModel.cases,
        [this, &sources, &output, useColor, &state](const TestCaseResult &testCase) -> void {
          renderCase(testCase, sources, output, useColor, state);
        });

    if (not options_.showSummary)
      return summary;

    if (summary.testCount != 0) {
      const usize passedWidth = summary.passedCaseCount * progressBarWidth / summary.caseCount;
      const usize failedWidth = progressBarWidth - passedWidth;

      output << '\n'
             << paint(String(passedWidth, '='), green, useColor)
             << paint(String(failedWidth, '='), red, useColor) << '\n';
    }

    output << std::format("\ntest result: {}. {}; {}; {}; {}; {}; {}; {}; finished in {}; {}; {}; "
                          "{}; {}; {}; wall {}\n",
        paint(summary.passed() ? "ok" : "FAILED", summary.passed() ? green : red, useColor),
        countLabel(summary.testCount, "test", "tests"),
        countLabel(summary.passedCount, "passed", "passed"),
        countLabel(summary.failedCount, "failed", "failed"),
        countLabel(summary.assertionCount, "assertion", "assertions"),
        countLabel(summary.failedAssertionCount, "failed assertions", "failed assertions"),
        countLabel(summary.errorCount, "error", "errors"),
        countLabel(summary.recoveredCount, "recovered case", "recovered cases"),
        durationLabel(summary.duration),
        countLabel(summary.caseCount, "case", "cases"),
        countLabel(summary.attemptCount, "attempt", "attempts"),
        countLabel(summary.sampleCount, "sample", "samples"),
        countLabel(summary.warmupCount, "warmup", "warmups"),
        countLabel(summary.retryCount, "retry", "retries"),
        durationLabel(summary.wallDuration));
    return summary;
  } else {
    return {};
  }
}

} // namespace Nyx::Test
