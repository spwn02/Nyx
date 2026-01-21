#include "Paths.h"

namespace Nyx {

static std::filesystem::path g_engineRoot;

static std::filesystem::path
findEngineRootFrom(const std::filesystem::path &start) {
  // Walk up until we find "engine/resources" as a marker.
  std::filesystem::path p = start;
  for (int i = 0; i < 8; ++i) {
    auto cand = p / "engine" / "resources";
    if (std::filesystem::exists(cand) && std::filesystem::is_directory(cand)) {
      return p / "engine";
    }
    if (!p.has_parent_path())
      break;
    p = p.parent_path();
  }
  // Fallback: assume CWD contains "engine/resources"
  auto cwd = std::filesystem::current_path() / "engine" / "resources";
  if (std::filesystem::exists(cwd) && std::filesystem::is_directory(cwd)) {
    return std::filesystem::current_path() / "engine";
  }
  return {}; // empty - caller should assert
}

void Paths::init(std::filesystem::path executablePath) {
  executablePath = std::filesystem::absolute(executablePath);
  const auto base = executablePath.has_parent_path()
                        ? executablePath.parent_path()
                        : std::filesystem::current_path();

  g_engineRoot = findEngineRootFrom(base);
  if (g_engineRoot.empty()) {
    // last resort: try current_path
    g_engineRoot = findEngineRootFrom(std::filesystem::current_path());
  }
}

const std::filesystem::path &Paths::engineRoot() { return g_engineRoot; }

std::filesystem::path Paths::engineRes() { return g_engineRoot / "resources"; }

std::filesystem::path Paths::engineShaders() { return engineRes() / "shaders"; }

std::filesystem::path Paths::shader(const std::string &file) {
  return engineShaders() / file;
}

} // namespace Nyx
