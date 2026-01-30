#pragma once
#include "editor/EditorState.h"

#include <string>

namespace Nyx {

struct ProjectSerializer final {
  static bool saveToFile(const EditorState &st, const std::string &path);
  static bool loadFromFile(EditorState &st, const std::string &path);
};

} // namespace Nyx
