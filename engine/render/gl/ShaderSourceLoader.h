#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nyx {

// Loads GLSL files from a root directory and expands #include "path".
// - include paths are resolved relative to shader root.
// - guards against include cycles.
// - caches expanded results.
class ShaderSourceLoader final {
public:
  struct LoadResult {
    bool ok = false;
    std::string expandedSource; // final GLSL source with includes expanded
    std::string debugName;
    std::vector<std::string> fileDeps; // absolute or canonical paths used
    std::string error;                 // human readable error
  };

  ShaderSourceLoader() = default;

  // Set shader root folder (e.g. "<repo>/engine/shaders")
  void setRoot(std::string shaderRootDir);

  const std::string &root() const { return m_root; }

  // Load and expand a shader file by path relative to root:
  // e.g. "passes/forward_plus.vert"
  LoadResult loadExpanded(const std::string &relativePath);

  // Cache control
  void clearCache();
  void setCacheEnabled(bool enabled) { m_cacheEnabled = enabled; }
  bool cacheEnabled() const { return m_cacheEnabled; }

private:
  struct CacheEntry {
    std::string expanded;
    std::vector<std::string> deps;
    std::uint64_t newestWriteTime = 0; // max write time among deps
  };

  std::string m_root;
  bool m_cacheEnabled = true;

  std::unordered_map<std::string, CacheEntry> m_cache; // key = relative path

  // internal helpers
  static std::string joinPath(const std::string &a, const std::string &b);
  static bool readTextFile(const std::string &absPath, std::string &out,
                           std::string &err);
  static std::uint64_t fileWriteTime(const std::string &absPath);

  bool expandRecursive(const std::string &absPath,
                       const std::string &logicalPathForErrors,
                       std::string &outExpanded,
                       std::vector<std::string> &outDeps,
                       std::vector<std::string> &includeStack,
                       std::string &outErr) const;

  bool parseInclude(const std::string &line, std::string &outIncludePath) const;

  bool isCacheValid(const CacheEntry &e) const;
};

} // namespace Nyx
