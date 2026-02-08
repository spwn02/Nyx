#include "EditorStateIO.h"

#include <cstddef>
#include <algorithm>

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
  if (st.viewport.outlineThicknessPx < 0.5f)
    st.viewport.outlineThicknessPx = 0.5f;
  if (st.viewport.outlineThicknessPx > 6.0f)
    st.viewport.outlineThicknessPx = 6.0f;
  if (st.projectFps < 1.0f)
    st.projectFps = 1.0f;
  if (st.animationLastFrame < 0)
    st.animationLastFrame = 0;
  if (st.animationFrame < 0)
    st.animationFrame = 0;
  if (st.animationFrame > st.animationLastFrame)
    st.animationFrame = st.animationLastFrame;
  if (st.animationClip.lastFrame < 0)
    st.animationClip.lastFrame = 0;
  if (st.animationClip.nextBlockId == 0)
    st.animationClip.nextBlockId = 1;
  for (auto &t : st.animationClip.tracks) {
    std::sort(t.curve.keys.begin(), t.curve.keys.end(),
              [](const AnimKey &a, const AnimKey &b) {
                return a.frame < b.frame;
              });
  }
  for (auto &r : st.animationClip.ranges) {
    if (r.end < r.start)
      std::swap(r.start, r.end);
  }

  for (size_t i = 0; i < st.recentScenes.size();) {
    if (st.recentScenes[i].empty())
      st.recentScenes.erase(st.recentScenes.begin() + (ptrdiff_t)i);
    else
      ++i;
  }
}

} // namespace Nyx
