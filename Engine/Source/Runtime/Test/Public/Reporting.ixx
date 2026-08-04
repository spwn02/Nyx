export module Nyx.Test:Reporting;

import std;
import Nyx.Core;
import :Execution;
import :Render;

export namespace Nyx::Test {

struct TestSummary final {
  usize testCount{};
  usize passedCount{};
  usize failedCount{};
  usize assertionCount{};
  usize failedAssertionCount{};
  usize errorCount{};
  std::chrono::steady_clock::duration duration{};

  [[nodiscard]] auto passed() const noexcept -> bool;

  [[nodiscard]] auto failed() const noexcept -> bool;
};

struct ReporterOptions final {
  RendererOptions renderer{};
  bool showPassedTests{};
  bool showSummary{true};
};

class Reporter final {
public:
  explicit Reporter(ReporterOptions options = {});

  auto addRoot(Path root) -> void;

  [[nodiscard]] static auto summarize(Span<const TestExecution> executions) noexcept -> TestSummary;

  auto report(Span<const TestExecution> executions, std::ostream &output) const -> TestSummary;

private:
  [[nodiscard]] auto colorEnabled() const noexcept -> bool;

  [[nodiscard]] auto shouldRenderTrace(const TestExecution &execution) const noexcept -> bool;

  auto renderFailure(const TestExecution &execution, std::ostream &output) const -> void;

  auto renderTrace(const TestExecution &execution, std::ostream &output) const -> void;

  SourceManager sources_;
  ReporterOptions options_{};
};

} // namespace Nyx::Test
