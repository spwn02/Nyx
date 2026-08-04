module Nyx.Test;

import :Diagnostics;
import :Render;

import std;
import Nyx.Core;

namespace Nyx::Test {

namespace {

[[nodiscard]] constexpr auto levelName(DiagnosticLevel level) noexcept -> StringView {
  switch (level) {
    case DiagnosticLevel::Error: return "error";
    case DiagnosticLevel::Warning: return "warning";
    case DiagnosticLevel::Note: return "note";
    case DiagnosticLevel::Help: return "help";
    case DiagnosticLevel::Marker: return "marker";
    default: return "diagnostic";
  }
}

[[nodiscard]] constexpr auto levelColor(DiagnosticLevel level) noexcept -> StringView {
  switch (level) {
    case DiagnosticLevel::Error: return "\x1b[1;31m";
    case DiagnosticLevel::Warning: return "\x1b[1;33m";
    case DiagnosticLevel::Note: return "\x1b[1;36m";
    case DiagnosticLevel::Help: return "\x1b[1;32m";
    case DiagnosticLevel::Marker: return "\x1b[1;34m";
    default: return "\x1b[1m";
  }
}

[[nodiscard]] constexpr auto spanColor(SpanKind kind) noexcept -> StringView {
  return kind == SpanKind::Primary ? "\x1b[1;31m" : "\x1b[1;36m";
}

constexpr inline StringView colorReset = "\x1b[0m";

[[nodiscard]] constexpr auto paint(StringView text, StringView color, bool useColor) -> String {
  if (not useColor)
    return String{text};

  String result{};
  result.reserve(color.size() + text.size() + colorReset.size());
  result.append(color);
  result.append(text);
  result.append(colorReset);
  return result;
}

[[nodiscard]] constexpr auto tabWidth(const RendererOptions &options) noexcept -> usize {
  return std::max<usize>(options.tabWidth, 1);
}

[[nodiscard]] constexpr auto visualColumn(StringView text, usize byteOffset, const RendererOptions &options)
    -> usize {
  usize column{};
  const usize end = std::min(text.size(), byteOffset);
  std::ranges::for_each(std::views::indices(end), [&](usize index) -> void {
    if (text[index] == '\t') {
      const usize width = tabWidth(options);
      column += width - (column % width);
    } else
      ++column;
  });
  return column;
}

[[nodiscard]] constexpr auto visualWidth(StringView text,
    usize begin,
    usize end,
    const RendererOptions &options) -> usize {
  const usize clampedBegin = std::min(begin, text.size());
  const usize clampedEnd = std::min(std::max(end, clampedBegin + 1), text.size());

  if (clampedEnd <= clampedBegin)
    return 1;

  return std::max(
      visualColumn(text, clampedEnd, options) - visualColumn(text, clampedBegin, options), usize{1});
}

[[nodiscard]] constexpr auto expandTabs(StringView text, const RendererOptions &options) -> String {
  String result{};
  result.reserve(text.size());
  usize column{};
  std::ranges::for_each(text, [&](const char character) -> void {
    if (character != '\t') {
      result.push_back(character);
      ++column;
      return;
    }

    const usize width = tabWidth(options);
    const usize amount = width - (column % width);
    result.append(amount, ' ');
    column += amount;
  });
  return result;
}

struct SourceGutter final {
  usize lineNumberWidth{1};

  [[nodiscard]] constexpr auto line(usize number) const -> String {
    return std::format("{:>{}} | ", number, lineNumberWidth);
  }

  [[nodiscard]] constexpr auto location() const -> String {
    String result(lineNumberWidth - 1, ' ');
    result.append(" --> ");
    return result;
  }

  [[nodiscard]] constexpr auto marker() const -> String {
    String result(lineNumberWidth + 1, ' ');
    result.append("| ");
    return result;
  }

  [[nodiscard]] constexpr auto guide() const -> String {
    String result(lineNumberWidth + 1, ' ');
    result.append("|\n");
    return result;
  }

  [[nodiscard]] constexpr auto detail() const -> String {
    String result(lineNumberWidth + 1, ' ');
    result.append("= ");
    return result;
  }
};

[[nodiscard]] constexpr auto decimalWidth(usize value) -> usize {
  return std::to_string(value).size();
}

[[nodiscard]] constexpr auto marker(StringView text, bool useColor) -> String {
  return paint(text, levelColor(DiagnosticLevel::Marker), useColor);
}

constexpr auto renderSpanTo(const SourceSpan &span,
    const SourceManager &sources,
    const RendererOptions &options,
    const SourceGutter &gutter,
    std::ostream &output,
    bool useColor) -> void {
  const auto line = static_cast<usize>(span.location.line());
  const usize column = std::max(static_cast<usize>(span.location.column()), usize{1});

  output << std::format(
      "{}{}:{}:{}\n", marker(gutter.location(), useColor), span.location.file_name(), line, column);

  if (not options.showSource)
    return;

  const Option<SourceLine> source = sources.sourceLine(span);
  output << marker(gutter.guide(), useColor);
  if (not source) {
    output << marker(gutter.marker(), useColor) << "source line unavailable\n";
  }

  const String sourcePrefix = gutter.line(source->number);
  output << marker(sourcePrefix, useColor) << expandTabs(source->text, options) << '\n';

  const usize begin = column - 1;
  const usize endColumn = span.endColumn > 0 ? span.endColumn : source->text.size();
  const usize end = endColumn > column ? endColumn - 1 : begin + 1;

  const usize prefix = visualColumn(source->text, begin, options);
  const usize width = visualWidth(source->text, begin, end, options);
  const char mark = span.kind == SpanKind::Primary ? '^' : '-';

  const String markerPrefix = gutter.marker();
  output << marker(markerPrefix, useColor) << String(prefix, ' ')
         << paint(String(width, mark), spanColor(span.kind), useColor);
  if (not span.label.empty())
    output << ' ' << paint(span.label, spanColor(span.kind), useColor);
  output << '\n';
}

[[nodiscard]] constexpr auto diagnosticGutter(const Diagnostic &diagnostic) -> SourceGutter {
  SourceGutter result{};
  std::ranges::for_each(diagnostic.details.spans | std::views::filter(isPrimarySpan),
      [&](const SourceSpan &span) constexpr noexcept -> void {
        result.lineNumberWidth =
            std::max(result.lineNumberWidth, decimalWidth(static_cast<usize>(span.location.line())));
      });
  return result;
}

constexpr auto renderNote(const DiagnosticNote &note,
    const SourceGutter &gutter,
    std::ostream &output,
    bool useColor) -> void {
  output << paint(
      std::format("{}{}:", gutter.detail(), levelName(note.level)), levelColor(note.level), useColor);
  if (not note.message.empty() or not note.fragments.empty())
    output << ' ';
  output << note.message;

  std::ranges::for_each(note.fragments, [&](const DiagnosticFragment &fragment) -> void {
    output << (fragment.highlighted ? paint(fragment.text, spanColor(SpanKind::Primary), useColor)
                                    : fragment.text);
  });
  output << '\n';
}

auto renderAttachment(const DiagnosticAttachment &attachment,
    const SourceGutter &gutter,
    std::ostream &output,
    bool useColor) -> void {
  output << std::format("{}attachment: {}\n", marker(gutter.detail(), useColor), attachment.name);
  if (attachment.content.empty())
    return;

  std::ranges::for_each(
      attachment.content | std::views::split('\n'), [&](const auto &line) constexpr -> void {
        output << marker(gutter.marker(), useColor) << String{line.begin(), line.end()};
      });
}

} // namespace

SourceManager::SourceManager() = default;

SourceManager::SourceManager(Vec<Path> roots)
    : roots_(std::move(roots)) {
}

auto SourceManager::addRoot(Path root) -> void {
  roots_.push_back(std::move(root));
}

auto SourceManager::exists(const Path &path) -> bool {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error);
}

auto SourceManager::resolve(const Path &requested) const -> Option<Path> {
  if (requested.is_absolute())
    return exists(requested) ? Option<Path>{requested} : None;

  if (exists(requested))
    return requested;

  const auto candidate = std::ranges::find_if(
      roots_, [&](const Path &root) constexpr -> bool { return exists(root / requested); });
  if (candidate == roots_.end())
    return None;

  return *candidate / requested;
}

auto SourceManager::sourceLine(const SourceSpan &span) const -> Option<SourceLine> {
  const std::source_location &location = span.location;
  if (location.line() == 0 or location.file_name() == nullptr or *location.file_name() == '\0')
    return None;

  const Option<Path> resolved = resolve(location.file_name());
  if (not resolved)
    return None;

  std::ifstream input(*resolved);
  if (not input)
    return None;

  String text{};
  const bool read = std::ranges::all_of(std::views::indices(static_cast<usize>(location.line())),
      [&input, &text](usize) -> bool { return static_cast<bool>(std::getline(input, text)); });
  if (not read)
    return None;

  if (not text.empty() and text.back() == '\r')
    text.pop_back();

  return SourceLine{
      .file = String{location.file_name()},
      .number = static_cast<usize>(location.line()),
      .text = std::move(text),
  };
}

AnsiRenderer::AnsiRenderer(RendererOptions options)
    : options_(options) {
}

auto AnsiRenderer::colorEnabled() const noexcept -> bool {
  switch (options_.color) {
    case ColorMode::Always: return true;
    case ColorMode::Automatic: return options_.terminal;
    case ColorMode::Never:
    default: return false;
  }
}

auto AnsiRenderer::render(const Diagnostic &diagnostic,
    const SourceManager &sources,
    std::ostream &output) const -> void {
  const bool useColor = colorEnabled();
  const SourceGutter gutter = diagnosticGutter(diagnostic);

  output << std::format("{}: {}\n",
      paint(std::format("{}[{}]", levelName(diagnostic.level), diagnostic.code()),
          levelColor(diagnostic.level),
          useColor),
      diagnostic.description());

  auto primarySpans = diagnostic.details.spans | std::views::filter(isPrimarySpan);
  bool renderedSpan{};

  std::ranges::for_each(primarySpans, [&](const SourceSpan &span) -> void {
    renderSpanTo(span, sources, options_, gutter, output, useColor);
    renderedSpan = true;
  });

  if (options_.details == DetailMode::None)
    return;

  auto secondarySpans =
      diagnostic.details.spans | std::views::filter([](const SourceSpan &span) constexpr noexcept -> bool {
        return not isPrimarySpan(span);
      });
  std::ranges::for_each(secondarySpans, [&](const SourceSpan &span) -> void {
    renderSpanTo(span, sources, options_, gutter, output, useColor);
    renderedSpan = true;
  });

  const bool hasDetails = not diagnostic.details.notes.empty() or not diagnostic.details.attachments.empty();
  if (renderedSpan and hasDetails)
    output << marker(gutter.guide(), useColor);

  std::ranges::for_each(diagnostic.details.notes, [&](const DiagnosticNote &note) -> void {
    renderNote(note, gutter, output, useColor);
    if (note.span)
      renderSpanTo(*note.span, sources, options_, gutter, output, useColor);
  });
  std::ranges::for_each(diagnostic.details.attachments, [&](const DiagnosticAttachment &attachment) -> void {
    renderAttachment(attachment, gutter, output, useColor);
  });
}

auto render(const Diagnostic &diagnostic,
    const SourceManager &sources,
    std::ostream &output,
    RendererOptions options) -> void {
  AnsiRenderer{options}.render(diagnostic, sources, output);
}

[[nodiscard]] auto renderToString(const Diagnostic &diagnostic,
    const SourceManager &sources,
    RendererOptions options) -> String {
  std::ostringstream output;
  render(diagnostic, sources, output, options);
  return output.str();
}

} // namespace Nyx::Test
