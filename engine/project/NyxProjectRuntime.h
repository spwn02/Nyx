#pragma once
#include "NyxProj.h"
#include "NyxProjIO.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Nyx {

// Small runtime helper that keeps project root/absolute resolution.
class NyxProjectRuntime final {
public:
  bool openProject(const std::string &absNyxprojPath) {
    auto r = NyxProjIO::load(absNyxprojPath);
    if (!r)
      return false;
    m_loaded = std::move(*r);
    return true;
  }

  bool saveProject(const std::string &absNyxprojPath) const {
    if (!m_loaded)
      return false;
    return NyxProjIO::save(absNyxprojPath, m_loaded->proj);
  }

  bool hasProject() const { return m_loaded.has_value(); }

  void createProject(const std::string &absNyxprojPath,
                     NyxProject project = NyxProject{}) {
    NyxProjLoadResult r{};
    r.proj = std::move(project);
    r.projectFileAbs = absNyxprojPath;
    r.projectDirAbs = NyxProjIO::dirname(absNyxprojPath);
    m_loaded = std::move(r);
  }

  const NyxProject &proj() const { return m_loaded->proj; }
  NyxProject &proj() { return m_loaded->proj; }

  const std::string &projectFileAbs() const { return m_loaded->projectFileAbs; }
  const std::string &projectDirAbs() const { return m_loaded->projectDirAbs; }
  const std::string &rootAbs() const { return projectDirAbs(); }

  // Resolve project-relative path to absolute.
  std::string resolveAbs(const std::string &rel) const {
    if (!m_loaded)
      return rel;
    return NyxProjIO::joinPath(m_loaded->projectDirAbs, rel);
  }

  // Common: "Content/..." absolute
  std::string contentDirAbs() const {
    if (!m_loaded)
      return {};
    return NyxProjIO::joinPath(m_loaded->projectDirAbs,
                               m_loaded->proj.assetRootRel);
  }

  std::string makeAbsolute(const std::string &rel) const {
    return resolveAbs(rel);
  }

  std::string makeRelative(const std::string &abs) const {
    if (!m_loaded)
      return abs;
    std::error_code ec;
    const auto rel = std::filesystem::relative(abs, m_loaded->projectDirAbs, ec);
    if (ec || rel.empty())
      return abs;
    return rel.lexically_normal().string();
  }

private:
  std::optional<NyxProjLoadResult> m_loaded;
};

} // namespace Nyx
