export module Nyx.Test:Json;

import std;
import Nyx.Core;
import :Execution;
import :Reporting;

export namespace Nyx::Test {

/// Controls the formatting of the stable Nyx.Test JSON report schema.
struct JsonReporterOptions final {
  /// Emits indentation and line breaks when enabled. Machine output is compact by default.
  bool pretty{};
  usize indentWidth{2};
};

/// Emits complete test-run state as JSON without ANSI colours or human-rendering policies.
///
/// The root object uses the current schema version. The schema deliberately keeps diagnostics, source spans,
/// notes, attachments, traces, execution seeds, and fixture-independent descriptors in the report so external
/// tools do not need to scrape the human renderer.
class JsonReporter final {
public:
  explicit JsonReporter(JsonReporterOptions options = {});

  auto addRoot(Path root) -> void;

  auto report(Span<const TestExecution> executions, std::ostream &output) const -> void;

  [[nodiscard]] auto render(Span<const TestExecution> executions) const -> String;

  /// Emits selected descriptors using the stable Nyx.Test JSON list schema.
  auto reportList(Span<const TestDescriptor> descriptors, std::ostream &output) const -> void;

  [[nodiscard]] auto renderList(Span<const TestDescriptor> descriptors) const -> String;

private:
  JsonReporterOptions options_{};
  Vec<Path> roots_;
};

} // namespace Nyx::Test
