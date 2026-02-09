#pragma once

#include <string>

namespace Nyx {

struct NyxProjectDesc final {
  std::string projectName;
  std::string startSceneRel; // "Content/Scenes/Main.nyxscene"
};

// Compatibility adapter over current NyxProjIO format.
class NyxProjectIO final {
public:
  static bool loadProject(const char *absPath, NyxProjectDesc &out);
  static bool saveProject(const char *absPath, const NyxProjectDesc &desc);
};

} // namespace Nyx
