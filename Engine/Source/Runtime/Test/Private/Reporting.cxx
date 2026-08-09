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

} // namespace

auto TestSummary::passed() const noexcept -> bool {
  return failedCount == 0;
}

auto TestSummary::failed() const noexcept -> bool {
  return not passed();
}

Reporter::Reporter(ReporterOptions options)
    : options_(options) {
}

auto Reporter::addRoot(Path root) -> void {
  sources_.addRoot(std::move(root));
}

auto Reporter::summarize(Span<const TestExecution> executions) noexcept -> TestSummary {
  TestSummary result{
      .testCount = executions.size(),
  };

  std::ranges::for_each(executions, [&result](const TestExecution &execution) constexpr -> void {
    result.duration += execution.duration;
    result.assertionCount += execution.state.assertions;
    result.failedAssertionCount += execution.state.failedAssertions;
    result.errorCount += execution.state.errors;

    if (execution.passed())
      ++result.passedCount;
    else
      ++result.failedCount;
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
}

auto Reporter::shouldRenderTrace(const TestExecution &execution) const noexcept -> bool {
  if (execution.state.traces.empty())
    return false;

  if (options_.renderer.details == DetailMode::Trace or execution.traceMode == TraceMode::ForcedAll)
    return true;

  return execution.traceMode == TraceMode::ForcedFailures and execution.failed();
}

auto Reporter::renderFailure(const TestExecution &execution, std::ostream &output) const -> void {
  std::ranges::for_each(execution.state.diagnostics, [&](const Diagnostic &diagnostic) -> void {
    render(withTestContext(execution.descriptor.identifier, diagnostic), sources_, output, options_.renderer);
  });
}

auto Reporter::renderTrace(const TestExecution &execution, std::ostream &output) const -> void {
  const bool useColor = colorEnabled();
  std::ranges::for_each(execution.state.traces, [&](const TraceEvent &event) -> void {
    output << paint("  = trace:", cyan, useColor) << ' ' << event.message;
    if (event.location.line() != 0)
      output << std::format(" ({}:{})", normalizePath(event.location.file_name()), event.location.line());

    output << '\n';
  });
}

auto Reporter::report(Span<const TestExecution> executions, std::ostream &output) const -> TestSummary {
  const bool useColor = colorEnabled();
  const TestSummary summary = summarize(executions);
  bool previousFailure{};

  std::ranges::for_each(executions, [&](const TestExecution &execution) -> void {
    const bool passed = execution.passed();
    const bool showTrace = shouldRenderTrace(execution);
    if (passed and not options_.showPassedTests and not showTrace)
      return;

    if (previousFailure)
      output << '\n';

    output << std::format("test {} ... {} {}\n",
        execution.descriptor.identifier,
        paint(passed ? "ok" : "FAILED", passed ? green : red, useColor),
        durationLabel(execution.duration));

    if (passed) {
      if (showTrace)
        renderTrace(execution, output);

      previousFailure = false;
      return;
    }

    output << '\n';
    if (showTrace)
      renderTrace(execution, output);

    renderFailure(execution, output);
    previousFailure = true;
  });

  if (not options_.showSummary)
    return summary;

  if (summary.testCount != 0) {
    const usize passedWidth = summary.passedCount * progressBarWidth / summary.testCount;
    const usize failedWidth = progressBarWidth - passedWidth;

    output << '\n'
           << paint(String(passedWidth, '='), green, useColor)
           << paint(String(failedWidth, '='), red, useColor) << '\n';
  }

  output << std::format("\ntest result: {}. {}; {}; {}; {}; {}; {}; finished in {}\n",
      paint(summary.passed() ? "ok" : "FAILED", summary.passed() ? green : red, useColor),
      countLabel(summary.testCount, "test", "tests"),
      countLabel(summary.passedCount, "passed", "passed"),
      countLabel(summary.failedCount, "failed", "failed"),
      countLabel(summary.assertionCount, "assertion", "assertions"),
      countLabel(summary.failedAssertionCount, "failed assertions", "failed assertions"),
      countLabel(summary.errorCount, "error", "errors"),
      durationLabel(summary.duration));
  return summary;
}

} // namespace Nyx::Test
