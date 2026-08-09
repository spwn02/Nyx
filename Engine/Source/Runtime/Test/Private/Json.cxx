module Nyx.Test;

import :Json;

import std;
import Nyx.Core;

namespace Nyx::Test {

namespace {

inline constexpr u32 schemaVersion{1};

struct JsonFrame final {
  bool first{true};
};

class JsonWriter final {
public:
  JsonWriter(std::ostream &output, JsonReporterOptions options)
      : output_(output)
      , options_(options) {
  }

  auto beginObject() -> void {
    output_ << '{';
    frames_.push_back(JsonFrame{});
  }

  auto endObject() -> void {
    const bool hasElements = not frames_.back().first;
    frames_.pop_back();
    closeContainer('}', hasElements);
  }

  auto beginArray() -> void {
    output_ << '[';
    frames_.push_back(JsonFrame{});
  }

  auto endArray() -> void {
    const bool hasElements = not frames_.back().first;
    frames_.pop_back();
    closeContainer(']', hasElements);
  }

  template <class Function>
  auto field(StringView name, Function &&function) -> void {
    prefix();
    text(name);
    output_ << ':';
    if (options_.pretty)
      output_ << ' ';
    std::invoke(std::forward<Function>(function));
  }

  template <class Function>
  auto element(Function &&function) -> void {
    prefix();
    std::invoke(std::forward<Function>(function));
  }

  auto text(StringView value) -> void {
    constexpr StringView hexadecimal{"0123456789ABCDEF"};

    output_ << '\"';
    std::ranges::for_each(value, [this, hexadecimal](char character) -> void {
      const Byte code = static_cast<Byte>(character);
      switch (code) {
        case '\"': output_ << "\\\""; return;
        case '\\': output_ << "\\\\"; return;
        case '\b': output_ << "\\b"; return;
        case '\f': output_ << "\\f"; return;
        case '\n': output_ << "\\n"; return;
        case '\r': output_ << "\\r"; return;
        case '\t': output_ << "\\t"; return;
        default: break;
      }

      // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
      if (code < 0x20) {
        output_ << "\\u00" << hexadecimal[code >> 4] << hexadecimal[code & 0x0F];
      }
      // NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

      output_ << character;
    });
    output_ << '\"';
  }

  auto boolean(bool value) -> void {
    output_ << (value ? "true" : "false");
  }

  auto nullValue() -> void {
    output_ << "null";
  }

  template <std::integral Value>
  auto number(Value value) -> void {
    output_ << std::format("{}", value);
  }

private:
  auto prefix() -> void {
    JsonFrame &frame = frames_.back();
    if (not frame.first)
      output_ << ',';

    frame.first = false;
    if (options_.pretty)
      output_ << '\n' << String(frames_.size() * options_.indentWidth, ' ');
  }

  auto closeContainer(char closing, bool hasElements) -> void {
    if (options_.pretty and hasElements)
      output_ << '\n' << String(frames_.size() * options_.indentWidth, ' ');

    output_ << closing;
  }

  std::ostream &output_; // NOLINT
  JsonReporterOptions options_{};
  Vec<JsonFrame> frames_;
};

[[nodiscard]] constexpr auto diagnosticLevelName(DiagnosticLevel level) noexcept -> StringView {
  switch (level) {
    case DiagnosticLevel::Error: return "error";
    case DiagnosticLevel::Warning: return "warning";
    case DiagnosticLevel::Note: return "note";
    case DiagnosticLevel::Help: return "help";
    case DiagnosticLevel::Marker: return "marker";
    default: return "unknown";
  }

  std::unreachable();
}

[[nodiscard]] constexpr auto spanKindName(SpanKind kind) noexcept -> StringView {
  switch (kind) {
    case SpanKind::Primary: return "primary";
    case SpanKind::Secondary: return "secondary";
    default: return "unknown";
  }

  std::unreachable();
}

[[nodiscard]] constexpr auto traceModeName(TraceMode mode) noexcept -> StringView {
  switch (mode) {
    case TraceMode::Annotations: return "annotations";
    case TraceMode::ForcedFailures: return "forced_failures";
    case TraceMode::ForcedAll: return "forced_all";
    default: return "unknown";
  }

  std::unreachable();
}

[[nodiscard]] constexpr auto executionStatus(const TestExecution &execution) noexcept -> StringView {
  return execution.passed() ? "passed" : "failed";
}

[[nodiscard]] constexpr auto durationNanoseconds(std::chrono::steady_clock::duration duration) noexcept
    -> i64 {
  return static_cast<i64>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

auto writeLocation(JsonWriter &writer, const std::source_location &location) -> void {
  writer.beginObject();
  writer.field("file", [&writer, &location] -> void {
    const char *file = location.file_name();
    writer.text(file == nullptr ? StringView{} : StringView{file});
  });
  writer.field("line", [&writer, &location] -> void { writer.number(location.line()); });
  writer.field("column", [&writer, &location] -> void { writer.number(location.column()); });
  writer.endObject();
}

auto writeSpan(JsonWriter &writer, const SourceSpan &span) -> void {
  writer.beginObject();
  writer.field("kind", [&writer, &span] -> void { writer.text(spanKindName(span.kind)); });
  writer.field("label", [&writer, &span] -> void { writer.text(span.label); });
  writer.field("end_column", [&writer, &span] -> void { writer.number(span.endColumn); });
  writer.field("location", [&writer, &span] -> void { writeLocation(writer, span.location); });
  writer.endObject();
}

auto writeOptionalSpan(JsonWriter &writer, const Option<SourceSpan> &span) -> void {
  if (span) {
    writeSpan(writer, *span);
    return;
  }

  writer.nullValue();
}

auto writeFragment(JsonWriter &writer, const DiagnosticFragment &fragment) -> void {
  writer.beginObject();
  writer.field("text", [&writer, &fragment] -> void { writer.text(fragment.text); });
  writer.field("highlighted", [&writer, &fragment] -> void { writer.boolean(fragment.highlighted); });
  writer.endObject();
}

auto writeNote(JsonWriter &writer, const DiagnosticNote &note) -> void {
  writer.beginObject();
  writer.field("level", [&writer, &note] -> void { writer.text(diagnosticLevelName(note.level)); });
  writer.field("message", [&writer, &note] -> void { writer.text(note.message); });
  writer.field("span", [&writer, &note] -> void { writeOptionalSpan(writer, note.span); });
  writer.field("fragments", [&writer, &note] -> void {
    writer.beginArray();
    std::ranges::for_each(note.fragments, [&writer](const DiagnosticFragment &fragment) -> void {
      writer.element([&writer, &fragment] -> void { writeFragment(writer, fragment); });
    });
    writer.endArray();
  });
  writer.endObject();
}

auto writeDiagnostic(JsonWriter &writer, const Diagnostic &diagnostic) -> void {
  writer.beginObject();
  writer.field(
      "level", [&writer, &diagnostic] -> void { writer.text(diagnosticLevelName(diagnostic.level)); });
  writer.field("code", [&writer, &diagnostic] -> void { writer.text(diagnostic.code()); });
  writer.field("description", [&writer, &diagnostic] -> void { writer.text(diagnostic.description()); });
  writer.field("spans", [&writer, &diagnostic] -> void {
    writer.beginArray();
    std::ranges::for_each(diagnostic.details.spans, [&writer](const SourceSpan &span) -> void {
      writer.element([&writer, &span] -> void { writeSpan(writer, span); });
    });
    writer.endArray();
  });
  writer.field("notes", [&writer, &diagnostic] -> void {
    writer.beginArray();
    std::ranges::for_each(diagnostic.details.notes, [&writer](const DiagnosticNote &note) -> void {
      writer.element([&writer, &note] -> void { writeNote(writer, note); });
    });
    writer.endArray();
  });
  writer.field("attachments", [&writer, &diagnostic] -> void {
    writer.beginArray();
    std::ranges::for_each(
        diagnostic.details.attachments, [&writer](const DiagnosticAttachment &attachment) -> void {
          writer.element([&writer, &attachment] -> void {
            writer.beginObject();
            writer.field("name", [&writer, &attachment] -> void { writer.text(attachment.name); });
            writer.field("content", [&writer, &attachment] -> void { writer.text(attachment.content); });
            writer.endObject();
          });
        });
    writer.endArray();
  });
  writer.endObject();
}

auto writeMetadata(JsonWriter &writer, const TestMetadata &metadata) -> void {
  writer.beginObject();
  writer.field("group", [&writer, &metadata] -> void {
    if (metadata.group)
      writer.text(*metadata.group);
    else
      writer.nullValue();
  });
  writer.field("tags", [&writer, &metadata] -> void {
    writer.beginArray();
    std::ranges::for_each(metadata.tags, [&writer](const String &tag) -> void {
      writer.element([&writer, &tag] -> void { writer.text(tag); });
    });
    writer.endArray();
  });
  writer.endObject();
}

auto writePolicy(JsonWriter &writer, const TestPolicy &policy) -> void {
  writer.beginObject();
  writer.field("trace", [&writer, &policy] -> void { writer.boolean(policy.trace); });
  writer.field("expected_panic", [&writer, &policy] -> void {
    if (policy.expectedPanic)
      writer.text(*policy.expectedPanic);
    else
      writer.nullValue();
  });
  writer.field("timeout_ns", [&writer, &policy] -> void {
    if (policy.timeout)
      writer.number(durationNanoseconds(*policy.timeout));
    else
      writer.nullValue();
  });
  writer.endObject();
}

auto writeDescriptor(JsonWriter &writer, const TestDescriptor &descriptor) -> void {
  writer.beginObject();
  writer.field("identifier", [&writer, &descriptor] -> void { writer.text(descriptor.identifier); });
  writer.field("name", [&writer, &descriptor] -> void { writer.text(descriptor.name); });
  writer.field("description", [&writer, &descriptor] -> void { writer.text(descriptor.description); });
  writer.field("test_case", [&writer, &descriptor] -> void { writer.number(descriptor.testCase); });
  writer.field("location", [&writer, &descriptor] -> void { writeLocation(writer, descriptor.location); });
  writer.field("metadata", [&writer, &descriptor] -> void { writeMetadata(writer, descriptor.metadata); });
  writer.field("policy", [&writer, &descriptor] -> void { writePolicy(writer, descriptor.policy); });
  writer.endObject();
}

auto writeState(JsonWriter &writer, const TestState &state) -> void {
  writer.beginObject();
  writer.field("assertions", [&writer, &state] -> void { writer.number(state.assertions); });
  writer.field("failed_assertions", [&writer, &state] -> void { writer.number(state.failedAssertions); });
  writer.field("errors", [&writer, &state] -> void { writer.number(state.errors); });
  writer.field("aborted", [&writer, &state] -> void { writer.number(state.aborted); });
  writer.field("diagnostics", [&writer, &state] -> void {
    writer.beginArray();
    std::ranges::for_each(state.diagnostics, [&writer](const Diagnostic &diagnostic) -> void {
      writer.element([&writer, &diagnostic] -> void { writeDiagnostic(writer, diagnostic); });
    });
    writer.endArray();
  });
  writer.field("traces", [&writer, &state] -> void {
    writer.beginArray();
    std::ranges::for_each(state.traces, [&writer](const TraceEvent &trace) -> void {
      writer.element([&writer, &trace] -> void {
        writer.beginObject();
        writer.field("message", [&writer, &trace] -> void { writer.text(trace.message); });
        writer.field("location", [&writer, &trace] -> void { writeLocation(writer, trace.location); });
        writer.endObject();
      });
    });
    writer.endArray();
  });
  writer.endObject();
}

auto writeExecution(JsonWriter &writer, const TestExecution &execution) -> void {
  writer.beginObject();
  writer.field("identifier", [&writer, &execution] -> void { writer.text(execution.descriptor.identifier); });
  writer.field("status", [&writer, &execution] -> void { writer.text(executionStatus(execution)); });
  writer.field("duration_ns",
      [&writer, &execution] -> void { writer.number(durationNanoseconds(execution.duration)); });
  writer.field("run_seed", [&writer, &execution] -> void { writer.number(execution.runSeed); });
  writer.field("seed", [&writer, &execution] -> void { writer.number(execution.seed); });
  writer.field("iteration", [&writer, &execution] -> void { writer.number(execution.iteration); });
  writer.field(
      "trace_mode", [&writer, &execution] -> void { writer.text(traceModeName(execution.traceMode)); });
  writer.field(
      "descriptor", [&writer, &execution] -> void { writeDescriptor(writer, execution.descriptor); });
  writer.field("state", [&writer, &execution] -> void { writeState(writer, execution.state); });
  writer.endObject();
}

auto writeSummary(JsonWriter &writer, const TestSummary &summary) -> void {
  writer.beginObject();
  writer.field(
      "status", [&writer, &summary] -> void { writer.text(summary.passed() ? "passed" : "failed"); });
  writer.field("tests", [&writer, &summary] -> void { writer.number(summary.testCount); });
  writer.field("passed", [&writer, &summary] -> void { writer.number(summary.passedCount); });
  writer.field("failed", [&writer, &summary] -> void { writer.number(summary.failedCount); });
  writer.field("assertions", [&writer, &summary] -> void { writer.number(summary.assertionCount); });
  writer.field(
      "failed_assertions", [&writer, &summary] -> void { writer.number(summary.failedAssertionCount); });
  writer.field("errors", [&writer, &summary] -> void { writer.number(summary.errorCount); });
  writer.field(
      "duration_ns", [&writer, &summary] -> void { writer.number(durationNanoseconds(summary.duration)); });
  writer.endObject();
}

auto writeReport(JsonWriter &writer, Span<const TestExecution> executions) -> void {
  const TestSummary summary = Reporter::summarize(executions);

  writer.beginObject();
  writer.field("schema_version", [&writer] -> void { writer.number(schemaVersion); });
  writer.field("framework", [&writer] -> void { writer.text("Nyx.Test"); });
  writer.field(
      "status", [&writer, &summary] -> void { writer.text(summary.passed() ? "passed" : "failed"); });
  writer.field("run_seed", [&writer, &executions] -> void {
    if (executions.empty())
      writer.nullValue();
    else
      writer.number(executions.front().runSeed);
  });
  writer.field("summary", [&writer, &summary] -> void { writeSummary(writer, summary); });
  writer.field("tests", [&writer, &executions] -> void {
    writer.beginArray();
    std::ranges::for_each(executions, [&writer](const TestExecution &execution) -> void {
      writer.element([&writer, &execution] -> void { writeExecution(writer, execution); });
    });
    writer.endArray();
  });
  writer.endObject();
}

} // namespace

JsonReporter::JsonReporter(JsonReporterOptions options)
    : options_(options) {
}

auto JsonReporter::report(Span<const TestExecution> executions, std::ostream &output) const -> void {
  JsonWriter writer{output, options_};
  writeReport(writer, executions);
  if (options_.pretty)
    output << '\n';
}

auto JsonReporter::render(Span<const TestExecution> executions) const -> String {
  std::ostringstream output{};
  report(executions, output);
  return output.str();
}

} // namespace Nyx::Test
