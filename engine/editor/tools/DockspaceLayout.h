#pragma once
#include "editor/EditorState.h"

namespace Nyx {

struct DockspaceLayout final {
  static void applyDefaultLayoutIfNeeded(EditorState &st, unsigned dockspaceId);
};

} // namespace Nyx
