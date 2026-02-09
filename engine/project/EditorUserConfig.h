#pragma once
#include "RecentProjects.h"
#include <optional>
#include <string>

namespace Nyx {

// Tiny editor-only config for things like recent projects.
// Saved next to imgui.ini OR in your preferred config dir.
struct EditorUserConfig final {
  RecentProjects recent;
};

// Minimal binary config: "NYXU" + ver + recents list.
// (Separate from .nyxproj on purpose.)
class EditorUserConfigIO final {
public:
  static bool save(const std::string &absPath, const EditorUserConfig &cfg);
  static std::optional<EditorUserConfig> load(const std::string &absPath);
};

} // namespace Nyx
