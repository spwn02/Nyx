#include "ShaderSourceLoader.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Nyx {

void ShaderSourceLoader::setRoot(std::string shaderRootDir) {
  m_root = std::move(shaderRootDir);
  // Normalize: remove trailing slashes
  while (!m_root.empty() && (m_root.back() == '/' || m_root.back() == '\\'))
    m_root.pop_back();
}

void ShaderSourceLoader::clearCache() { m_cache.clear(); }

static std::string canonicalStr(const std::filesystem::path &p) {
  std::error_code ec;
  auto cp = std::filesystem::weakly_canonical(p, ec);
  if (ec)
    return p.lexically_normal().string();
  return cp.string();
}

std::string ShaderSourceLoader::joinPath(const std::string &a,
                                         const std::string &b) {
  std::filesystem::path p = std::filesystem::path(a) / std::filesystem::path(b);
  return p.lexically_normal().string();
}

bool ShaderSourceLoader::readTextFile(const std::string &absPath,
                                      std::string &out, std::string &err) {
  err.clear();
  out.clear();

  std::ifstream file(absPath, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    err = "Failed to open file: " + absPath;
    return false;
  }

  std::ostringstream ss;
  ss << file.rdbuf();
  out = ss.str();
  return true;
}

std::uint64_t ShaderSourceLoader::fileWriteTime(const std::string &absPath) {
  std::error_code ec;
  auto ftime = std::filesystem::last_write_time(absPath, ec);
  if (ec)
    return 0;

  // Convert to uint64-ish (stable enough for cache invalidation)
  auto s = ftime.time_since_epoch().count();
  if (s < 0)
    s = 0;
  return static_cast<std::uint64_t>(s);
}

bool ShaderSourceLoader::isCacheValid(const CacheEntry &e) const {
  if (e.deps.empty())
    return false;

  std::uint64_t newest = 0;
  for (const std::string &dep : e.deps) {
    newest = std::max(newest, fileWriteTime(dep));
  }
  return newest == e.newestWriteTime;
}

ShaderSourceLoader::LoadResult
ShaderSourceLoader::loadExpanded(const std::string &relativePath) {
  LoadResult r{};
  r.debugName = relativePath;

  if (m_root.empty()) {
    r.ok = false;
    r.error = "ShaderSourceLoader root not set";
    return r;
  }

  // Cache
  if (m_cacheEnabled) {
    auto it = m_cache.find(relativePath);
    if (it != m_cache.end() && isCacheValid(it->second)) {
      r.ok = true;
      r.expandedSource = it->second.expanded;
      r.fileDeps = it->second.deps;
      return r;
    }
  }

  // Resolve absolute
  const std::string absPath =
      canonicalStr(std::filesystem::path(joinPath(m_root, relativePath)));

  std::string expanded;
  std::vector<std::string> deps;
  std::vector<std::string> stack;
  std::string err;

  if (!expandRecursive(absPath, relativePath, expanded, deps, stack, err)) {
    r.ok = false;
    r.error = err;
    return r;
  }

  // Update cache entry
  if (m_cacheEnabled) {
    CacheEntry e{};
    e.expanded = expanded;
    e.deps = deps;

    std::uint64_t newest = 0;
    for (const std::string &dep : deps)
      newest = std::max(newest, fileWriteTime(dep));
    e.newestWriteTime = newest;

    m_cache[relativePath] = std::move(e);
  }

  r.ok = true;
  r.expandedSource = std::move(expanded);
  r.fileDeps = std::move(deps);
  return r;
}

bool ShaderSourceLoader::parseInclude(const std::string &line,
                                      std::string &outIncludePath) const {
  outIncludePath.clear();

  // Accept:
  //   #include "common/foo.glsl"
  //   #include   "x.glsl"
  // Ignore includes in commented lines (cheap: check if line starts with //)
  auto trimLeft = [](const std::string &s) -> size_t {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
      ++i;
    return i;
  };

  const size_t start = trimLeft(line);
  if (start >= line.size())
    return false;

  if (line.compare(start, 2, "//") == 0)
    return false;

  if (line.compare(start, 8, "#include") != 0)
    return false;

  size_t q1 = line.find('"', start + 8);
  if (q1 == std::string::npos)
    return false;
  size_t q2 = line.find('"', q1 + 1);
  if (q2 == std::string::npos)
    return false;

  outIncludePath = line.substr(q1 + 1, q2 - q1 - 1);
  return !outIncludePath.empty();
}

bool ShaderSourceLoader::expandRecursive(
    const std::string &absPath, const std::string &logicalPathForErrors,
    std::string &outExpanded, std::vector<std::string> &outDeps,
    std::vector<std::string> &includeStack, std::string &outErr) const {
  outErr.clear();

  // Cycle guard
  const std::string absCanon = canonicalStr(std::filesystem::path(absPath));
  if (std::find(includeStack.begin(), includeStack.end(), absCanon) !=
      includeStack.end()) {
    outErr = "Shader include cycle detected while including: " + absCanon;
    return false;
  }

  includeStack.push_back(absCanon);

  std::string src;
  std::string readErr;
  if (!readTextFile(absCanon, src, readErr)) {
    outErr = readErr + " (logical: " + logicalPathForErrors + ")";
    includeStack.pop_back();
    return false;
  }

  outDeps.push_back(absCanon);

  // Expand line-by-line
  std::istringstream iss(src);
  std::string line;

  while (std::getline(iss, line)) {
    std::string inc;
    if (parseInclude(line, inc)) {
      // Resolve include path relative to shader root (not relative to the
      // includer) This keeps includes stable and easy.
      const std::string incAbs =
          canonicalStr(std::filesystem::path(joinPath(m_root, inc)));

      // Inject a #line directive so compiler errors point into the included
      // file.
      outExpanded += "\n#line 1\n";

      if (!expandRecursive(incAbs, inc, outExpanded, outDeps, includeStack,
                           outErr)) {
        includeStack.pop_back();
        return false;
      }

      // Back to includer file:
      outExpanded += "\n#line 1\n";
    } else {
      outExpanded += line;
      outExpanded += "\n";
    }
  }

  includeStack.pop_back();
  return true;
}

} // namespace Nyx
