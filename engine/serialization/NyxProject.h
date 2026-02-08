#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx {

struct AssetMount {
  std::string virtualRoot; // "/Game"
  std::string diskPath;    // "Content"
};

struct EditorState {
  float cameraSpeed = 5.0f;
  bool showGrid = true;
  uint32_t gizmoMode = 0; // 0 = translate, 1 = rotate, 2 = scale
};

class NyxProject {
public:
  std::string name;
  std::string startupScene; // virtual path
  std::string lastScene;    // editor only

  std::vector<AssetMount> mounts;
  EditorState editor;
};

} // namespace Nyx
