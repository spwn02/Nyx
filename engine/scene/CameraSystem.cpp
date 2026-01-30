#include "CameraSystem.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Nyx {

static void ensureSize(uint32_t &w, uint32_t &h) {
  if (w == 0)
    w = 1;
  if (h == 0)
    h = 1;
}

void CameraSystem::update(World &world, uint32_t viewportW,
                          uint32_t viewportH) {
  ensureSize(viewportW, viewportH);
  const float aspect = float(viewportW) / float(viewportH);

  for (EntityID e : world.alive()) {
    if (!world.hasCamera(e))
      continue;

    auto &cam = world.camera(e);
    auto &mats = world.cameraMatrices(e);

    const bool sizeChanged =
        (mats.lastW != viewportW || mats.lastH != viewportH);

    // Treat transform changes as camera dirty too (for view matrix)
    if (cam.dirty || sizeChanged)
      mats.dirty = true;

    if (!mats.dirty)
      continue;

    world.updateTransforms();
    const glm::mat4 W = world.worldTransform(e).world;

    // Camera view is inverse of world matrix
    mats.view = glm::inverse(W);
    if (cam.projection == CameraProjection::Perspective) {
      mats.proj = glm::perspective(glm::radians(cam.fovYDeg), aspect, cam.nearZ,
                                   cam.farZ);
    } else {
      const float h = (cam.orthoHeight <= 0.0001f) ? 0.0001f : cam.orthoHeight;
      const float w = h * aspect;
      mats.proj = glm::ortho(-w * 0.5f, w * 0.5f, -h * 0.5f, h * 0.5f,
                             cam.nearZ, cam.farZ);
    }
    mats.viewProj = mats.proj * mats.view;

    mats.dirty = false;
    cam.dirty = false;
    mats.lastW = viewportW;
    mats.lastH = viewportH;
  }
}

glm::mat4 CameraSystem::activeViewProj(World &world, uint32_t viewportW,
                                       uint32_t viewportH) {
  const EntityID camE = world.activeCamera();
  if (camE == InvalidEntity || !world.hasCamera(camE))
    return glm::mat4(1.0f);

  update(world, viewportW, viewportH);
  return world.cameraMatrices(camE).viewProj;
}

} // namespace Nyx
