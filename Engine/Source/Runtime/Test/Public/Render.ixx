export module Nyx.Test:Render;

import std;
import Nyx.Core;
import :Diagnostics;

export namespace Nyx::Test {

struct SourceLine final {
  Path file;
  usize number{};
  String text;
};

/// Resolves source locations and loads only the lines needed by a diagnostic.
///
/// The manager owns no process-wide cache. That keeps rendering deterministic and independent renderer
/// instances safe to use on different threads.
class SourceManager final {
public:
  SourceManager();
  explicit SourceManager(Vec<Path> roots);

  auto addRoot(Path root) -> void;

  [[nodiscard]] auto sourceLine(const SourceSpan &span) const -> Option<SourceLine>;

private:
  [[nodiscard]] static auto exists(const Path &path) -> bool;

  [[nodiscard]] auto resolve(const Path &requested) const -> Option<Path>;

  Vec<Path> roots_;
};

enum class[[= debug::derive]] ColorMode : u8 {
  Automatic,
  Always,
  Never,
};

struct RendererOptions final {
  ColorMode color{ColorMode::Automatic};
  bool terminal{};
  bool showSource{true};
  DetailMode details{DetailMode::Failures};
  usize tabWidth{4};
};

class AnsiRenderer final {
public:
  explicit AnsiRenderer(RendererOptions options = {});

  auto render(const Diagnostic &diagnostic, const SourceManager &sources, std::ostream &output) const -> void;

private:
  [[nodiscard]] auto colorEnabled() const noexcept -> bool;

  RendererOptions options_{};
};

auto render(const Diagnostic &diagnostic,
    const SourceManager &sources,
    std::ostream &output,
    RendererOptions options = {}) -> void;

[[nodiscard]] auto renderToString(const Diagnostic &diagnostic,
    const SourceManager &sources,
    RendererOptions options = {}) -> String;

} // namespace Nyx::Test
