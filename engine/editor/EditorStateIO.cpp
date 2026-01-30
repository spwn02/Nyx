#include "EditorStateIO.h"

#include <cstddef>

namespace Nyx {

void EditorStateIO::onSceneOpened(EditorState &st,
                                 const std::string &scenePath) {
  st.lastScenePath = scenePath;
  st.pushRecentScene(scenePath);
}

void EditorStateIO::sanitizeBeforeSave(EditorState &st) {
  if (st.viewport.msaa == 0)
    st.viewport.msaa = 1;
  if (st.viewport.msaa > 16)
    st.viewport.msaa = 16;

  for (size_t i = 0; i < st.recentScenes.size();) {
    if (st.recentScenes[i].empty())
      st.recentScenes.erase(st.recentScenes.begin() + (ptrdiff_t)i);
    else
      ++i;
  }
}

} // namespace Nyx
