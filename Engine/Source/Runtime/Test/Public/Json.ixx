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
/// The root object uses schema_version == 1. The schema deliberately keeps diagnostics, source spans, notes,
/// attachments, traces, execution seeds, and fixture-independent descriptors in the report so external tools
/// do not need to scrape the human renderer.
class JsonReporter final {
public:
  explicit JsonReporter(JsonReporterOptions options = {});

  auto report(Span<const TestExecution> executions, std::ostream &output) const -> void;

  [[nodiscard]] auto render(Span<const TestExecution> executions) const -> String;

private:
  JsonReporterOptions options_{};
};

} // namespace Nyx::Test
