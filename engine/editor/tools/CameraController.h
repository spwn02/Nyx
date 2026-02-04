#pragma once

#include "platform/GLFWWindow.h"
#include "scene/EntityID.h"
#include "scene/World.h"
#include <glm/glm.hpp>

namespace Nyx {

class EngineContext;
class AppContext;

struct EditorCameraController final {
  glm::vec3 position{0.0f, 1.5f, 3.0f};
  float yawDeg = -90.0f;
  float pitchDeg = 0.0f;

  float fovYDeg = 60.0f;
  float nearZ = 0.01f;
  float farZ = 2000.0f;

  float speed = 6.0f;
  float boostMul = 2.0f;
  float sensitivity = 0.12f;

  bool mouseCaptured = false;

  void captureMouse(bool captured, GLFWWindow &w);

  void tick(EngineContext &engine, AppContext &app, float dt);
  void apply(World &world, EntityID camEnt);
};

} // namespace Nyx
