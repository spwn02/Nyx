#pragma once
#include "editor/EditorState.h"

namespace Nyx {

struct EditorStateIO final {
  static void onSceneOpened(EditorState &st, const std::string &scenePath);
  static void sanitizeBeforeSave(EditorState &st);
};

} // namespace Nyx
