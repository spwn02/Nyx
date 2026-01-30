#include "editor/LockCameraToView.h"

#include "scene/Camera.h"
#include "scene/World.h"

#include <glm/gtc/quaternion.hpp>

namespace Nyx {

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

static void editorFromWorldMatrix(EditorCameraState &cam,
                                  const glm::mat4 &world) {
  cam.position = glm::vec3(world[3]);

  const glm::vec3 forward = glm::normalize(-glm::vec3(world[2]));
  cam.yawDeg = glm::degrees(std::atan2(forward.z, forward.x));
  cam.pitchDeg = glm::degrees(std::asin(forward.y));
}

void LockCameraToView::onToggled(World &world, EntityID activeCam,
                                 EditorCameraState &editorCam) {
  if (!enabled || !snapEditorToSceneOnEnable)
    return;
  if (activeCam == InvalidEntity || !world.hasCamera(activeCam))
    return;

  world.updateTransforms();
  const glm::mat4 W = world.worldTransform(activeCam).world;
  editorFromWorldMatrix(editorCam, W);
}

void LockCameraToView::tick(World &world, EntityID activeCam,
                            const EditorCameraState &editorCam) {
  if (!enabled || !driveSceneCameraFromEditor)
    return;
  if (activeCam == InvalidEntity || !world.hasCamera(activeCam))
    return;

  auto &tr = world.transform(activeCam);
  tr.translation = editorCam.position;
  tr.rotation = cameraRotation(cameraFront(editorCam.yawDeg, editorCam.pitchDeg));
  tr.scale = glm::vec3(1.0f);
  tr.dirty = true;

  auto &cam = world.ensureCamera(activeCam);
  cam.dirty = true;
}

} // namespace Nyx
