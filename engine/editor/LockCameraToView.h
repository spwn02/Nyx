#pragma once

#include "scene/EntityID.h"
#include <glm/glm.hpp>

namespace Nyx {

class World;

struct EditorCameraState final {
  glm::vec3 position{0.0f};
  float yawDeg = 0.0f;
  float pitchDeg = 0.0f;
};

struct LockCameraToView final {
  bool enabled = false;
  bool snapEditorToSceneOnEnable = true;
  bool driveSceneCameraFromEditor = true;

  void onToggled(World &world, EntityID activeCam,
                 EditorCameraState &editorCam);
  void tick(World &world, EntityID activeCam,
            const EditorCameraState &editorCam);
};

} // namespace Nyx
