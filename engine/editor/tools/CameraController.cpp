#include "CameraController.h"
#include "app/AppContext.h"
#include "app/EngineContext.h"
#include "editor/EditorLayer.h"
#include "input/InputSystem.h"
#include "platform/GLFWWindow.h"
#include "scene/EntityID.h"
#include "scene/World.h"

#include "core/Log.h"

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
  const auto &ed = app.editorLayer();
  const bool viewThrough = ed->viewThroughCamera();
  const bool lockToView = ed->lockCameraToView().enabled;

  // If viewing through another camera without lock, don't update anything
  if (viewThrough && !lockToView) {
    return;
  }

  auto &input = app.window().input();

  const bool shift = isShiftDown(input);
  const bool orbiting = input.isDown(Key::MouseMiddle) && !shift;
  const bool panningMouse = input.isDown(Key::MouseMiddle) && shift;

  const double mdx = input.state().mouseDeltaX;
  const double mdy = input.state().mouseDeltaY;
  const double sdx = input.state().scrollX;
  const double sdy = input.state().scrollY;
  const bool ctrl = input.isDown(Key::LeftCtrl) || input.isDown(Key::RightCtrl);

  const glm::vec3 front = cameraFront(yawDeg, pitchDeg);
  const glm::vec3 right =
      glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
  const glm::vec3 up = glm::normalize(glm::cross(right, front));

  distance = std::max(distance, 0.01f);

  if (orbiting) {
    yawDeg += float(mdx) * sensitivity;
    pitchDeg -= float(mdy) * sensitivity;
    pitchDeg = std::clamp(pitchDeg, -89.0f, 89.0f);
  }

  // Shift + MMB pan (mouse).
  if (panningMouse) {
    const float panScale = distance * 0.0025f;
    const float dx = float(mdx);
    const float dy = float(mdy);
    center += (-right * dx + up * dy) * panScale;
  }

  // Touchpad behavior (Linux): infer pinch for zoom, otherwise orbit.
  // Default scroll = orbit. Shift+scrollY moves center forward.
  if (sdx != 0.0 || sdy != 0.0) {
    // Refresh scroll mode when we see deltas.
    scrollModeTimer = 0.25f;

    if (ctrl) {
      scrollMode = ScrollMode::Zoom;
    } else if (scrollMode == ScrollMode::None) {
      const float ax = std::abs(float(sdx));
      const float ay = std::abs(float(sdy));
      // Heuristic: strong vertical intent -> zoom, otherwise orbit.
      if (ay > ax * 1.5f && ay > 0.2f) {
        scrollMode = ScrollMode::Zoom;
      } else {
        scrollMode = ScrollMode::Pan; // use "Pan" as "Orbit" mode
      }
    }

    if (shift) {
      const float panScale = distance * 0.0025f;
      const float forwardScale = distance * 0.05f;
      if (sdx != 0.0) {
        center += (-right * float(sdx) * 30.0f) * panScale;
      }
      if (sdy != 0.0) {
        center += front * float(sdy) * forwardScale;
      }
    } else if (scrollMode == ScrollMode::Zoom) {
      const float zoomScale = std::max(0.05f, distance * 0.1f);
      distance -= float(sdy) * zoomScale;
      distance = std::max(distance, 0.05f);
    } else {
      // Orbit with scroll deltas.
      yawDeg += float(sdx) * 100.0f * sensitivity;
      pitchDeg -= float(sdy) * 100.0f * sensitivity;
      pitchDeg = std::clamp(pitchDeg, -89.0f, 89.0f);
    }
  } else {
    if (scrollModeTimer > 0.0f) {
      scrollModeTimer = std::max(0.0f, scrollModeTimer - dt);
      if (scrollModeTimer == 0.0f) {
        scrollMode = ScrollMode::None;
      }
    }
  }

  // Recompute position from updated yaw/pitch/center/distance.
  const glm::vec3 newFront = cameraFront(yawDeg, pitchDeg);
  position = center - newFront * distance;

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

} // namespace Nyx
