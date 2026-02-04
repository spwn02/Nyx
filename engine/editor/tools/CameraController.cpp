#include "CameraController.h"
#include "app/AppContext.h"
#include "app/EngineContext.h"
#include "core/Log.h"
#include "editor/EditorLayer.h"
#include "input/InputSystem.h"
#include "platform/GLFWWindow.h"
#include "scene/EntityID.h"
#include "scene/World.h"

namespace Nyx {

void EditorCameraController::captureMouse(bool capture, GLFWWindow &w) {
  if (capture) {
    if (mouseCaptured)
      return;
    w.disableCursor(true);
    mouseCaptured = true;
  } else {
    if (!mouseCaptured)
      return;
    w.disableCursor(false);
    mouseCaptured = false;
  }
}

static glm::vec3 cameraFront(float yawDeg, float pitchDeg) {
  const float yaw = glm::radians(yawDeg);
  const float pitch = glm::radians(pitchDeg);
  glm::vec3 f{cos(yaw) * cos(pitch), sin(pitch), sin(yaw) * cos(pitch)};
  return glm::normalize(f);
}

static glm::quat cameraRotation(const glm::vec3 &front) {
  const glm::vec3 up(0.0f, 1.0f, 0.0f);
  const glm::vec3 f = glm::normalize(front);
  const glm::vec3 r = glm::normalize(glm::cross(f, up));
  const glm::vec3 u = glm::cross(r, f);
  glm::mat3 m(1.0f);
  m[0] = r;
  m[1] = u;
  m[2] = -f;
  return glm::quat_cast(m);
}

static bool isShiftDown(const InputSystem &in) {
  return in.isDown(Key::LeftShift) || in.isDown(Key::RightShift);
}

void EditorCameraController::apply(World &world, EntityID camEnt) {
  if (camEnt == InvalidEntity || !world.isAlive(camEnt) ||
      !world.hasCamera(camEnt))
    return;

  auto &tr = world.transform(camEnt);
  tr.translation = position;
  tr.rotation = cameraRotation(cameraFront(yawDeg, pitchDeg));
  tr.dirty = true;

  auto &cam = world.ensureCamera(camEnt);
  cam.fovYDeg = fovYDeg;
  cam.nearZ = nearZ;
  cam.farZ = farZ;
  cam.dirty = true;
}

void EditorCameraController::tick(EngineContext &engine, AppContext &app,
                                  float dt) {
  if (mouseCaptured) {
    // ImGui's GLFW backend may reset the cursor mode each frame; re-apply.
    app.window().disableCursor(true);

    const auto &ed = app.editorLayer();
    const bool viewThrough = ed->viewThroughCamera();
    const bool lockToView = ed->lockCameraToView().enabled;

    // If viewing through another camera without lock, don't update anything
    if (viewThrough && !lockToView) {
      return;
    }

    auto &input = app.window().input();

    yawDeg += float(input.state().mouseDeltaX) * sensitivity;
    pitchDeg -= float(input.state().mouseDeltaY) * sensitivity;
    pitchDeg = std::clamp(pitchDeg, -120.0f, 120.0f);

    const glm::vec3 front = cameraFront(yawDeg, pitchDeg);
    const glm::vec3 right =
        glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));

    float sp = speed * dt;
    if (isShiftDown(input))
      sp *= boostMul;

    glm::vec3 moveDelta(0.0f);
    if (input.isDown(Key::W))
      moveDelta += front * sp;
    if (input.isDown(Key::S))
      moveDelta -= front * sp;
    if (input.isDown(Key::A))
      moveDelta -= right * sp;
    if (input.isDown(Key::D))
      moveDelta += right * sp;
    if (input.isDown(Key::Q))
      moveDelta.y -= sp;
    if (input.isDown(Key::E))
      moveDelta.y += sp;

    if (moveDelta != glm::vec3(0.0f)) {
      position += moveDelta;
    }

    if (viewThrough && lockToView) {
      // Apply movement to the active camera when lock-to-view is enabled
      const EntityID active = engine.world().activeCamera();
      if (active != InvalidEntity && active != ed->cameraEntity()) {
        EditorCameraState camState{};
        camState.position = position;
        camState.yawDeg = yawDeg;
        camState.pitchDeg = pitchDeg;
        ed->lockCameraToView().tick(engine.world(), active, camState);
      }
    } else {
      // Normal mode: apply to editor camera
      apply(engine.world(), ed->cameraEntity());
    }
  }
}

} // namespace Nyx
