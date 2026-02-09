#pragma once
#include "NyxProj.h"
#include <cstdint>
#include <optional>
#include <string>

namespace Nyx {

struct NyxProjLoadResult final {
  NyxProject proj{};
  std::string projectFileAbs; // path used to load
  std::string projectDirAbs;  // directory of .nyxproj
};

class NyxProjIO final {
public:
  // Save .nyxproj to absolute path
  static bool save(const std::string &absPath, const NyxProject &proj);

  // Load .nyxproj from absolute path
  static std::optional<NyxProjLoadResult> load(const std::string &absPath);

  // Helpers
  static std::string joinPath(const std::string &a, const std::string &b);
  static std::string dirname(const std::string &absPath);
};

} // namespace Nyx
