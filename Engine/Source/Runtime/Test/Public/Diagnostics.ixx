export module Nyx.Test:Diagnostics;

import std;
import Nyx.Core;
import :Annotations;

export namespace Nyx::Test {

enum class[[= debug::derive]] DiagnosticLevel : u8 {
  Error,
  Warning,
  Note,
  Help,
  Marker,
};

enum class[[= debug::derive]] SpanKind : u8 {
  Primary,
  Secondary,
};

enum class[[ = debug::derive, = diagnostics::prefix("NYX") ]] DiagnosticCode : u8 {
  /// Deliberately has no message: the renderer falls back to its debug-derived name.
  Unknown = 0,

  AssertionFailed[[= diagnostics::message("assertion failed")]] = 1,
  UnhandledException[[= diagnostics::message("unhandled exception")]] = 10,
  TestReturnedError[[= diagnostics::message("test returned an error")]] = 11,
  TestPanicked[[= diagnostics::message("test panicked")]] = 12,
  TimeoutExceeded[[= diagnostics::message("test exceeded its timeout")]] = 13,
  ExpectedPanicNotObserved[[= diagnostics::message("expected panic was not observed")]] = 14,
  ProviderProducedNoValues[[= diagnostics::message("provider produced no values")]] = 15,
  TaskStranded[[= diagnostics::message("asynchronous task was stranded")]] = 16,
};

enum class[[= debug::derive]] DetailMode : u8 {
  None,
  Failures,
  Trace,
};

struct SourceSpan final {
  std::source_location location;
  /// One-past-the-end, one-based source column. Zero denotes a one-column marker.
  usize endColumn{};
  SpanKind kind{SpanKind::Primary};
  String label;
};

[[nodiscard]] auto makeSpan(String label = {},
    SpanKind = SpanKind::Primary,
    std::source_location location = std::source_location::current(),
    usize endColumn = 0) -> SourceSpan;

[[nodiscard]] auto isPrimarySpan(const SourceSpan &span) noexcept -> bool;

struct DiagnosticFragment final {
  String text;
  bool highlighted{};
};

struct DiagnosticNote final {
  DiagnosticLevel level{DiagnosticLevel::Note};
  String message;
  Option<SourceSpan> span;
  Vec<DiagnosticFragment> fragments;
};

struct DiagnosticAttachment final {
  String name;
  String content;
};

struct DiagnosticHeader final {
  DiagnosticCode code{};
  Option<String> descriptionOverride;
};

struct DiagnosticDetails final {
  Vec<SourceSpan> spans;
  Vec<DiagnosticNote> notes;
  Vec<DiagnosticAttachment> attachments;
};

struct Diagnostic final {
  DiagnosticLevel level{DiagnosticLevel::Error};
  DiagnosticHeader header{};
  DiagnosticDetails details{};

  [[nodiscard]] auto code() const -> String;

  [[nodiscard]] auto description() const -> String;

  [[nodiscard]] auto primarySpan() const noexcept -> Option<Ref<const SourceSpan>>;

  auto addSpan(SourceSpan span) -> Diagnostic &;

  auto addNote(String note,
      DiagnosticLevel noteLevel = DiagnosticLevel::Note,
      Option<SourceSpan> noteSpan = None) -> Diagnostic &;

  auto addNote(Vec<DiagnosticFragment> fragments,
      DiagnosticLevel noteLevel = DiagnosticLevel::Note,
      Option<SourceSpan> noteSpan = None) -> Diagnostic &;

  auto addAttachment(String name, String content) -> Diagnostic &;
};

[[nodiscard]] constexpr auto diagnosticCode(DiagnosticCode code) -> String {
  StringView prefix = diagnostics::annotationPrefix<^^DiagnosticCode>();
  return std::format("{}{:03}", prefix, std::to_underlying(code));
}

[[nodiscard]] constexpr auto diagnosticDescription(DiagnosticCode code) -> String {
  // NOLINTNEXTLINE(bugprone-reserved-identifier)
  template for (constexpr std::meta::info item : meta::enumerators<^^DiagnosticCode>) {
    if ([:item:] == code)
      return String{diagnostics::annotationMessage<item>()};
  }

  return std::format("{}", code);
}

[[nodiscard]] auto makeDiagnostic(DiagnosticCode code,
    std::source_location location = std::source_location::current()) -> Diagnostic;

[[nodiscard]] auto makeDiagnostic(DiagnosticLevel level,
    DiagnosticCode code,
    std::source_location location = std::source_location::current()) -> Diagnostic;

} // namespace Nyx::Test
