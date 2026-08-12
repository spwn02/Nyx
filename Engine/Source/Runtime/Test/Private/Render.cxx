module Nyx.Test;

import :Diagnostics;
import :Render;

import std;
import Nyx.Core;

namespace Nyx::Test {

namespace {

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

  if (not options.showSource or not has(options.effectiveSections(), DiagnosticSection::Source))
    return;

  const Option<SourceResolution> resolution = sources.resolve(span);
  output << marker(gutter.guide(), useColor);
  if (not resolution) {
    output << marker(gutter.marker(), useColor) << "source line unavailable\n";
    return;
  }

  const SourceDocument &document = resolution->document.get();
  const SourceRange &range = resolution->range;
  const SourceRange &highlight = resolution->highlight;
  const usize firstLine = range.begin.line;
  const usize lastLine = std::max(range.end.line, firstLine);
  std::ranges::for_each(std::views::iota(firstLine, lastLine + 1), [&](usize number) -> void {
    const StringView text = document.line(number);
    output << marker(gutter.line(number), useColor) << expandTabs(text, options) << '\n';
  });

  const char mark = span.kind == SpanKind::Primary ? '^' : '-';

  const String markerPrefix = gutter.marker();
  const usize firstHighlightLine = std::min(std::max(highlight.begin.line, firstLine), lastLine);
  const usize lastHighlightLine = std::min(std::max(highlight.end.line, firstHighlightLine), lastLine);
  std::ranges::for_each(
      std::views::iota(firstHighlightLine, lastHighlightLine + 1), [&](usize number) -> void {
        const StringView text = document.line(number);
        const usize begin =
            number == highlight.begin.line ? std::max<usize>(highlight.begin.column, 1) - 1 : usize{};
        const usize end =
            number == highlight.end.line ? std::max<usize>(highlight.end.column, begin + 2) - 1 : text.size();
        const usize prefix = visualColumn(text, begin, options);
        const usize width = visualWidth(text, begin, end, options);

        output << marker(markerPrefix, useColor) << String(prefix, ' ')
               << paint(String(width, mark), spanColor(span.kind), useColor);
        if (number == firstHighlightLine and not span.label.empty())
          output << ' ' << paint(span.label, spanColor(span.kind), useColor);
        output << '\n';
      });
}

[[nodiscard]] constexpr auto diagnosticGutter(const Diagnostic &diagnostic, const SourceManager &sources)
    -> SourceGutter {
  SourceGutter result{};
  std::ranges::for_each(diagnostic.details.spans, [&](const SourceSpan &span) -> void {
    result.lineNumberWidth =
        std::max(result.lineNumberWidth, decimalWidth(static_cast<usize>(span.location.line())));

    const Option<SourceResolution> resolution = sources.resolve(span);
    if (resolution)
      result.lineNumberWidth = std::max(result.lineNumberWidth, decimalWidth(resolution->range.end.line));
  });
  return result;
}

constexpr auto renderExpansion(const DiagnosticExpansion &expansion,
    const SourceGutter &gutter,
    std::ostream &output,
    bool useColor) -> void {
  std::visit(
      [&](const auto &expansion) -> void {
        using Type = std::remove_cvref_t<decltype(expansion)>;
        if constexpr (std::same_as<Type, BinaryExpansion>) {
          const BinaryExpansion &value = expansion;
          output << paint(gutter.detail(), levelColor(DiagnosticLevel::Note), useColor)
                 << "expanded to: " << value.left << ' '
                 << paint(value.operatorName, levelColor(DiagnosticLevel::Error), useColor) << ' '
                 << value.right << '\n';
        }
        if constexpr (std::same_as<Type, ContainsExpansion>) {
          const ContainsExpansion &value = expansion;
          output << paint(gutter.detail(), levelColor(DiagnosticLevel::Note), useColor)
                 << paint(value.needle, levelColor(DiagnosticLevel::Error), useColor) << " was not found in "
                 << value.container << '\n';
        }
        if constexpr (std::same_as<Type, NearExpansion>) {
          const NearExpansion &value = expansion;
          output << paint(gutter.detail(), levelColor(DiagnosticLevel::Note), useColor) << "expanded to: |"
                 << value.left << " - " << value.right
                 << "| = " << paint(value.difference, levelColor(DiagnosticLevel::Error), useColor)
                 << ", exceeding tolerance " << value.tolerance << '\n';
        }
      },
      expansion);
}

constexpr auto renderNote(const DiagnosticNote &note,
    const SourceGutter &gutter,
    std::ostream &output,
    bool useColor) -> void {
  output << paint(
      std::format("{}{}:", gutter.detail(), debug::enumName(note.level)), levelColor(note.level), useColor);
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

enum class LexicalMode : u8 {
  Normal,
  LineComment,
  BlockComment,
  String,
  Character,
  RawString,
};

struct LexicalState final {
  LexicalMode mode{LexicalMode::Normal};
  bool escaped{};
  String rawDelimiter;
};

[[nodiscard]] constexpr auto matchingDelimiter(char value) noexcept -> char {
  switch (value) {
    case '(': return ')';
    case '[': return ']';
    case '{': return '}';
    default: return '\0';
  }

  std::unreachable();
}

[[nodiscard]] auto rawStringEnd(StringView text, usize start, String &delimiter) -> Option<usize> {
  if (start + 1 >= text.size() or text[start] != 'R' or text[start + 1] != '"')
    return None;

  const usize opening = text.find('(', start + 2);
  if (opening == StringView::npos)
    return None;

  delimiter = String{text.substr(start + 2, opening - start - 2)};
  const String terminator = std::format("){}\"", delimiter);
  const usize closing = text.find(terminator, opening + 1);
  if (closing == StringView::npos)
    return text.size();

  return closing + terminator.size();
}

[[nodiscard]] auto lineStart(StringView text, usize offset) -> usize {
  const usize clamped = std::min(offset, text.size());
  if (clamped == 0)
    return 0;

  const usize newline = text.rfind('\n', clamped - 1);
  return newline == StringView::npos ? 0 : newline + 1;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
[[nodiscard]] auto findToken(StringView text, usize start, char expected) -> Option<usize> {
  LexicalState state{};
  usize index = start;

  while (index < text.size()) {
    const char current = text[index];

    if (state.mode == LexicalMode::LineComment) {
      if (current == '\n') {
        if (state.escaped)
          state.escaped = false;
        else
          state.mode = LexicalMode::Normal;
        ;
      } else {
        state.escaped = current == '\\';
      }
      ++index;
      continue;
    }

    if (state.mode == LexicalMode::BlockComment) {
      if (current == '*' and index + 1 < text.size() and text[index + 1] == '/') {
        state.mode = LexicalMode::Normal;
        index += 2;
      } else {
        ++index;
      }
      continue;
    }

    if (state.mode == LexicalMode::String or state.mode == LexicalMode::Character) {
      if (state.escaped) {
        state.escaped = false;
      } else if (current == '\\') {
        state.escaped = true;
      } else if ((state.mode == LexicalMode::String and current == '"') or
                 (state.mode == LexicalMode::Character and current == '\'')) {
        state.mode = LexicalMode::Normal;
      }
      ++index;
      continue;
    }

    if (state.mode == LexicalMode::RawString) {
      const String terminator = std::format("){}\"", state.rawDelimiter);
      if (text.substr(index, terminator.size()) == terminator) {
        state.mode = LexicalMode::Normal;
        index += terminator.size();
      } else {
        ++index;
      }
      continue;
    }

    if (current == '/' and index + 1 < text.size() and text[index + 1] == '/') {
      state.mode = LexicalMode::LineComment;
      index += 2;
      continue;
    }

    if (current == '/' and index + 1 < text.size() and text[index + 1] == '*') {
      state.mode = LexicalMode::BlockComment;
      index += 2;
      continue;
    }

    if (current == 'R' and index + 1 < text.size() and text[index + 1] == '"') {
      const Option<usize> end = rawStringEnd(text, index, state.rawDelimiter);
      if (end) {
        state.mode = LexicalMode::Normal;
        index = *end;
        continue;
      }
    }

    if (current == '"') {
      state.mode = LexicalMode::String;
      state.escaped = false;
      ++index;
      continue;
    }

    if (current == '\'') {
      state.mode = LexicalMode::Character;
      state.escaped = false;
      ++index;
      continue;
    }

    if (current == expected)
      return index;

    ++index;
  }
  return None;
}

[[nodiscard]] auto statementStart(StringView text, usize offset) -> usize {
  const usize clamped = std::min(offset, text.size());
  StringView prefix = text.substr(0, clamped);
  usize boundary{};
  usize search{};

  while (const Option<usize> found = findToken(prefix, search, ';')) {
    boundary = *found + 1;
    search = boundary;
  }

  return lineStart(text, boundary);
}

inline constexpr usize delimitersCount{32};

// NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
[[nodiscard]] auto scanToBoundary(StringView text,
    usize start,
    bool stopAtOpeningBrace,
    bool *foundBoundary = nullptr) -> usize {
  LexicalState state{};
  InplaceVec<char, delimitersCount> delimiters{};
  usize index = start;

  while (index < text.size()) {
    const char current = text[index];

    if (state.mode == LexicalMode::LineComment) {
      if (current == '\n') {
        if (state.escaped)
          state.escaped = false;
        else
          state.mode = LexicalMode::Normal;
      } else {
        state.escaped = current == '\\';
      }
      ++index;
      continue;
    }

    if (state.mode == LexicalMode::BlockComment) {
      if (current == '*' and index + 1 < text.size() and text[index + 1] == '/') {
        state.mode = LexicalMode::Normal;
        index += 2;
      } else {
        ++index;
      }
      continue;
    }

    if (state.mode == LexicalMode::String or state.mode == LexicalMode::Character) {
      if (state.escaped) {
        state.escaped = false;
      } else if (current == '\\') {
        state.escaped = true;
      } else if ((state.mode == LexicalMode::String and current == '"') or
                 (state.mode == LexicalMode::Character and current == '\'')) {
        state.mode = LexicalMode::Normal;
      }
      ++index;
      continue;
    }

    if (state.mode == LexicalMode::RawString) {
      const String terminator = std::format("){}\"", state.rawDelimiter);
      if (text.substr(index, terminator.size()) == terminator) {
        state.mode = LexicalMode::Normal;
        index += terminator.size();
      } else {
        ++index;
      }
      continue;
    }

    if (current == '/' and index + 1 < text.size() and text[index + 1] == '/') {
      state.mode = LexicalMode::LineComment;
      index += 2;
      continue;
    }

    if (current == '/' and index + 1 < text.size() and text[index + 1] == '*') {
      state.mode = LexicalMode::BlockComment;
      index += 2;
      continue;
    }

    if (current == 'R' and index + 1 < text.size() and text[index + 1] == '"') {
      const Option<usize> end = rawStringEnd(text, index, state.rawDelimiter);
      if (end) {
        state.mode = LexicalMode::Normal;
        index = *end;
        continue;
      }
    }

    if (current == '"') {
      state.mode = LexicalMode::String;
      state.escaped = false;
      ++index;
      continue;
    }

    if (current == '\'') {
      state.mode = LexicalMode::Character;
      state.escaped = false;
      ++index;
      continue;
    }

    if (stopAtOpeningBrace and current == '{' and delimiters.empty()) {
      if (foundBoundary != nullptr)
        *foundBoundary = true;
      return index + 1;
    }

    if (current == ';' and delimiters.empty()) {
      if (foundBoundary != nullptr)
        *foundBoundary = true;
      return index + 1;
    }

    const char closing = matchingDelimiter(current);
    if (closing != '\0') {
      delimiters.push_back(closing);
      ++index;
      continue;
    }

    if ((current == ')' or current == ']' or current == '}') and not delimiters.empty() and
        delimiters.back() == current) {
      delimiters.pop_back();
    }

    ++index;
  }

  return text.size();
}

// NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
[[nodiscard]] auto scanInvocationEnd(StringView text, usize start) -> usize {
  const Option<usize> opening = findToken(text, start, '(');
  if (not opening)
    return scanToBoundary(text, start, false);

  LexicalState state{};
  InplaceVec<char, delimitersCount> delimiters{};
  delimiters.push_back(')');
  usize index = *opening + 1;

  while (index < text.size()) {
    const char current = text[index];

    if (state.mode == LexicalMode::LineComment) {
      if (current == '\n') {
        if (state.escaped)
          state.escaped = false;
        else
          state.mode = LexicalMode::Normal;
      } else {
        state.escaped = current == '\\';
      }
      ++index;
      continue;
    }

    if (state.mode == LexicalMode::BlockComment) {
      if (current == '*' and index + 1 < text.size() and text[index + 1] == '/') {
        state.mode = LexicalMode::Normal;
        index += 2;
      } else {
        ++index;
      }
      continue;
    }

    if (state.mode == LexicalMode::String or state.mode == LexicalMode::Character) {
      if (state.escaped) {
        state.escaped = false;
      } else if (current == '\\') {
        state.escaped = true;
      } else if ((state.mode == LexicalMode::String and current == '"') or
                 (state.mode == LexicalMode::Character and current == '\'')) {
        state.mode = LexicalMode::Normal;
      }
      ++index;
      continue;
    }

    if (state.mode == LexicalMode::RawString) {
      const String terminator = std::format("){}\"", state.rawDelimiter);
      if (text.substr(index, terminator.size()) == terminator) {
        state.mode = LexicalMode::Normal;
        index += terminator.size();
      } else {
        ++index;
      }
      continue;
    }

    if (current == '/' and index + 1 < text.size() and text[index + 1] == '/') {
      state.mode = LexicalMode::LineComment;
      index += 2;
      continue;
    }

    if (current == '/' and index + 1 < text.size() and text[index + 1] == '*') {
      state.mode = LexicalMode::BlockComment;
      index += 2;
      continue;
    }

    if (current == 'R' and index + 1 < text.size() and text[index + 1] == '"') {
      const Option<usize> end = rawStringEnd(text, index, state.rawDelimiter);
      if (end) {
        state.mode = LexicalMode::Normal;
        index = *end;
        continue;
      }
    }

    if (current == '"') {
      state.mode = LexicalMode::String;
      state.escaped = false;
      ++index;
      continue;
    }

    if (current == '\'') {
      state.mode = LexicalMode::Character;
      state.escaped = false;
      ++index;
      continue;
    }

    const char closing = matchingDelimiter(current);
    if (closing != '\0') {
      delimiters.push_back(closing);
      ++index;
      continue;
    }

    if ((current == ')' or current == ']' or current == '}') and not delimiters.empty() and
        delimiters.back() == current) {
      delimiters.pop_back();
      if (delimiters.empty())
        return index + 1;
    }

    ++index;
  }

  return text.size();
}

struct Invocation final {
  usize begin{};
  usize opening{};
  usize end{};
};

[[nodiscard]] constexpr auto isIdentifierCharacter(char value) noexcept -> bool {
  return (value >= 'a' and value <= 'z') or (value >= 'A' and value <= 'Z') or
         (value >= '0' and value <= '9') or value == '_';
}

[[nodiscard]] constexpr auto isWhitespace(char value) noexcept -> bool {
  return value == ' ' or value == '\t' or value == '\n' or value == '\r';
}

[[nodiscard]] auto invocationBegin(StringView text, usize opening) -> usize {
  usize begin = opening;
  while (begin > 0 and (isIdentifierCharacter(text[begin - 1]) or text[begin - 1] == ':'))
    --begin;

  return begin;
}

[[nodiscard]] auto invocationName(StringView text, const Invocation &invocation) -> StringView {
  usize end = invocation.opening;
  while (end > invocation.begin and isWhitespace(text[end - 1]))
    --end;

  usize begin = end;
  while (begin > invocation.begin and isIdentifierCharacter(text[begin - 1]))
    --begin;

  return text.substr(begin, end - begin);
}

[[nodiscard]] auto findInvocation(StringView text, usize start) -> Option<Invocation> {
  const Option<usize> opening = findToken(text, start, '(');
  if (not opening)
    return None;

  const usize begin = invocationBegin(text, *opening);
  return Invocation{
      .begin = begin,
      .opening = *opening,
      .end = scanInvocationEnd(text, begin),
  };
}

[[nodiscard]] auto isAssertionInvocation(StringView name) noexcept -> bool {
  return name == "check" or name == "require";
}

[[nodiscard]] auto isInsideAttribute(StringView text, usize offset) -> bool {
  const usize opening = text.rfind("[[", offset);
  const usize closing = text.rfind("]]", offset);
  return opening != StringView::npos and (closing == StringView::npos or opening > closing);
}

[[nodiscard]] auto isDeclarationSuffix(StringView name) noexcept -> bool {
  return name == "noexcept" or name == "requires";
}

[[nodiscard]] auto declarationHighlight(StringView text, usize begin, usize end) -> Pair<usize, usize> {
  Option<Invocation> candidate;
  usize search = begin;

  while (const Option<Invocation> invocation = findInvocation(text, search)) {
    if (invocation->opening >= end)
      break;

    const StringView name = invocationName(text, *invocation);
    if (not isInsideAttribute(text, invocation->begin) and not isDeclarationSuffix(name))
      candidate = invocation;
    search = std::max(invocation->end, invocation->opening + 1);
  }

  if (not candidate)
    return {begin, std::min(begin + 1, text.size())};

  return {candidate->begin, candidate->end};
}

struct SourceOffsets final {
  Pair<usize, usize> range;
  Pair<usize, usize> highlight;
};

[[nodiscard]] auto sourceOffsets(const SourceDocument &document, const SourceSpan &span) -> SourceOffsets {
  if (document.lineOffsets.empty())
    return {};

  const usize line = span.location.line() == 0 ? 1 : static_cast<usize>(span.location.line());
  const usize column = std::max(static_cast<usize>(span.location.column()), usize{1});
  const usize lineIndex = std::min(line - 1, document.lineOffsets.size() - 1);
  const usize anchor = std::min(document.lineOffsets[lineIndex] + column - 1, document.contents.size());

  if (span.selection == SpanSelection::Point)
    return SourceOffsets{
        .range = {anchor, std::min(anchor + 1, document.contents.size())},
        .highlight = {anchor, std::min(anchor + 1, document.contents.size())},
    };

  const usize begin = span.selection == SpanSelection::EnclosingStatement
                          ? statementStart(document.contents, anchor)
                          : lineStart(document.contents, anchor);
  const Pair<usize, usize> fallback{anchor, std::min(anchor + 1, document.contents.size())};
  bool foundBoundary{};
  const usize end =
      scanToBoundary(document.contents, begin, span.selection == SpanSelection::Declaration, &foundBoundary);

  if (not foundBoundary)
    return SourceOffsets{
        .range = {begin, fallback.second},
        .highlight = fallback,
    };

  if (span.selection == SpanSelection::Declaration)
    return SourceOffsets{
        .range = {begin, std::max(end, fallback.second)},
        .highlight = declarationHighlight(document.contents, begin, end),
    };

  const Option<Invocation> invocation = findInvocation(document.contents, anchor);
  if (not invocation)
    return SourceOffsets{
        .range = {begin, std::max(end, fallback.second)},
        .highlight = fallback,
    };

  if (span.selection == SpanSelection::Invocation)
    return SourceOffsets{
        .range = {begin, std::max(end, invocation->end)},
        .highlight = {invocation->begin, invocation->end},
    };

  if (span.selection == SpanSelection::EnclosingStatement)
    return SourceOffsets{
        .range = {begin, std::max(end, invocation->end)},
        .highlight = {invocation->begin, invocation->end},
    };

  const StringView name = invocationName(document.contents, *invocation);
  if (isAssertionInvocation(name)) {
    const Option<Invocation> nested = findInvocation(document.contents, invocation->opening + 1);
    if (nested and nested->begin < invocation->end)
      return SourceOffsets{
          .range = {begin, std::max(end, invocation->end)},
          .highlight = {nested->begin, nested->end},
      };
  }

  return SourceOffsets{
      .range = {begin, std::max(end, invocation->end)},
      .highlight = {invocation->begin, invocation->end},
  };
}

[[nodiscard]] auto sourcePosition(const SourceDocument &document, usize offset) -> SourcePosition {
  const usize clamped = std::min(offset, document.contents.size());
  const auto line = std::ranges::upper_bound(document.lineOffsets, clamped);
  const usize lineIndex =
      line == document.lineOffsets.begin()
          ? 0
          : static_cast<usize>(std::ranges::distance(document.lineOffsets.begin(), line) - 1);
  return SourcePosition{
      .line = lineIndex + 1,
      .column = clamped - document.lineOffsets[lineIndex] + 1,
  };
}

} // namespace

SourceManager::SourceManager() = default;

SourceManager::SourceManager(Vec<Path> roots)
    : roots_(std::move(roots)) {
}

auto SourceManager::addRoot(Path root) -> void {
  roots_.push_back(std::move(root));
}

// NOLINTNEXTLINE(bugprone-exception-escape)
auto SourceDocument::line(usize number) const noexcept -> StringView {
  if (number == 0 or number > lineOffsets.size())
    return {};

  const usize index = number - 1;
  const usize begin = lineOffsets[index];
  const usize end = index + 1 < lineOffsets.size() ? lineOffsets[index + 1] : contents.size();
  const usize length = end > begin and contents[end - 1] == '\n' ? end - begin - 1 : end - begin;
  const usize trimmed = length > 0 and contents[begin + length - 1] == '\r' ? length - 1 : length;
  return StringView{contents}.substr(begin, trimmed);
}

auto SourceDocument::lineCount() const noexcept -> usize {
  return lineOffsets.size();
  ;
}

auto SourceManager::exists(const Path &path) -> bool {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error);
}

auto SourceManager::resolvePath(const Path &requested) const -> Option<Path> {
  if (requested.is_absolute())
    return exists(requested) ? Option<Path>{requested} : None;

  if (exists(requested))
    return requested;

  const auto candidate =
      std::ranges::find_if(roots_, [&](const Path &root) -> bool { return exists(root / requested); });
  if (candidate == roots_.end())
    return None;

  return *candidate / requested;
}

auto SourceManager::load(const Path &path) const -> Option<Ref<const SourceDocument>> {
  if (const auto existing = documents_.find(path); existing != documents_.end())
    return std::cref(*existing->second);

  std::ifstream input(path, std::ios::binary);
  if (not input)
    return None;

  String contents{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  Vec<usize> lineOffsets{0};
  std::ranges::for_each(std::views::indices(contents.size()), [&](usize index) -> void {
    if (contents[index] == '\n')
      lineOffsets.push_back(index + 1);
  });

  auto [document, inserted] = documents_.emplace(path,
      std::make_unique<SourceDocument>(SourceDocument{
          .path = path,
          .contents = std::move(contents),
          .lineOffsets = std::move(lineOffsets),
      }));
  static_cast<void>(inserted);
  return std::cref(*document->second);
}

auto SourceManager::resolve(const SourceSpan &span) const -> Option<SourceResolution> {
  const std::source_location &location = span.location;
  if (location.line() == 0 or location.file_name() == nullptr or *location.file_name() == '\0')
    return None;

  const Option<Path> resolved = resolvePath(location.file_name());
  if (not resolved)
    return None;

  const Option<Ref<const SourceDocument>> document = load(*resolved);
  if (not document)
    return None;

  const SourceDocument &source = document->get();
  if (static_cast<usize>(location.line()) > source.lineCount())
    return None;

  const SourceOffsets offsets = sourceOffsets(source, span);
  const auto makeRange = [&source, &resolved](Pair<usize, usize> positions) -> SourceRange {
    return SourceRange{
        .file = *resolved,
        .begin = sourcePosition(source, positions.first),
        .end = sourcePosition(source, positions.second),
    };
  };

  return SourceResolution{
      .document = document->get(),
      .range = makeRange(offsets.range),
      .highlight = makeRange(offsets.highlight),
  };
}

auto SourceManager::sourceLine(const SourceSpan &span) const -> Option<SourceLine> {
  const Option<SourceResolution> resolution = resolve(span);
  if (not resolution)
    return None;

  const SourceDocument &document = resolution->document.get();
  const usize number = span.location.line() == 0 ? 1 : static_cast<usize>(span.location.line());
  const StringView text = document.line(number);
  if (text.empty() and number > document.lineCount())
    return None;

  return SourceLine{
      .file = document.path,
      .number = number,
      .text = String{text},
  };
}

constexpr auto RendererOptions::effectiveSections() const noexcept -> DiagnosticSections {
  if (sections)
    return *sections;

  const DiagnosticSections failureSections =
      DiagnosticSection::Header | DiagnosticSection::Source | DiagnosticSection::PrimarySpan |
      DiagnosticSection::SecondarySpans | DiagnosticSection::Notes | DiagnosticSection::Attachments;
  switch (details) {
    case DetailMode::None: return DiagnosticSection::Header;
    case DetailMode::Trace: return failureSections | DiagnosticSection::Trace;
    case DetailMode::Failures:
    default: return failureSections;
  }
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
  const SourceGutter gutter = diagnosticGutter(diagnostic, sources);
  const DiagnosticSections sections = options_.effectiveSections();

  if (has(sections, DiagnosticSection::Header))
    output << std::format("{}: {}\n",
        paint(std::format("{}[{}]", debug::enumName(diagnostic.level), diagnostic.code()),
            levelColor(diagnostic.level),
            useColor),
        diagnostic.description());

  auto primarySpans =
      diagnostic.details.spans | std::views::filter([&](const SourceSpan &span) constexpr noexcept -> bool {
        return isPrimarySpan(span) and has(sections, DiagnosticSection::PrimarySpan);
      });
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
  if (has(sections, DiagnosticSection::SecondarySpans))
    std::ranges::for_each(secondarySpans, [&](const SourceSpan &span) -> void {
      renderSpanTo(span, sources, options_, gutter, output, useColor);
      renderedSpan = true;
    });

  const bool hasNotes = has(sections, DiagnosticSection::Notes) and
                        (not diagnostic.details.notes.empty() or
                            not std::holds_alternative<std::monostate>(diagnostic.details.expansion));
  const bool hasAttachments =
      has(sections, DiagnosticSection::Attachments) and not diagnostic.details.attachments.empty();
  if (renderedSpan and (hasNotes or hasAttachments))
    output << marker(gutter.guide(), useColor);

  if (hasNotes) {
    renderExpansion(diagnostic.details.expansion, gutter, output, useColor);
    std::ranges::for_each(diagnostic.details.notes, [&](const DiagnosticNote &note) -> void {
      renderNote(note, gutter, output, useColor);
      if (note.span)
        renderSpanTo(*note.span, sources, options_, gutter, output, useColor);
    });
  }

  if (hasAttachments)
    std::ranges::for_each(
        diagnostic.details.attachments, [&](const DiagnosticAttachment &attachment) -> void {
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
