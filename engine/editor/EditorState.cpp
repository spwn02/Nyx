#include "EditorState.h"

#include <algorithm>

namespace Nyx {

void EditorState::pushRecentScene(const std::string &path) {
  if (path.empty())
    return;

  recentScenes.erase(
      std::remove(recentScenes.begin(), recentScenes.end(), path),
      recentScenes.end());

  recentScenes.insert(recentScenes.begin(), path);

  constexpr size_t kMax = 16;
  if (recentScenes.size() > kMax)
    recentScenes.resize(kMax);
}

} // namespace Nyx
