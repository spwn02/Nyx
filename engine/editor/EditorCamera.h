#pragma once

#include "glm/fwd.hpp"
#include <cstdint>
#include <glm/glm.hpp>

namespace Nyx {

// Y-up, right-handed editor camera.
// Cache: view/proj/viewProj and recompute only when dirty.
struct EditorCamera final {
  // Pose
  glm::vec3 position{0.0f, 1.5f, 3.0f};
  float yawDeg = -90.0f;
  float pitchDeg = 0.0f;

  // Projection
  float fovYDeg = 60.0f;
  float nearZ = 0.01f;
  float farZ = 2000.0f;

  // Movement tuning
  float speed = 6.0f;
  float boostMul = 2.0f;
  float sensitivity = 0.12f; // deg per pixel (tweakable)

  bool mouseCaptured = false;

  // Viewport driving projection aspect
  uint32_t viewportW = 1;
  uint32_t viewportH = 1;

  // Cached matrices
  const glm::mat4 &view() const {
    updateIfDirty();
    return m_view;
  }
  const glm::mat4 &proj() const {
    updateIfDirty();
    return m_proj;
  }
  const glm::mat4 &viewProj() const {
    updateIfDirty();
    return m_viewProj;
  }
  float aspect() const {
    return (viewportH > 0) ? (float(viewportW) / float(viewportH)) : 1.0f;
  }

  glm::vec3 front() const;
  glm::vec3 right() const;
  glm::vec3 up() const { return glm::vec3(0.0f, 1.0f, 0.0f); }

  // Mark dirty explicitly
  void markViewDirty();
  void markProjDirty();

  // Call when viewport size changes
  void setViewport(uint32_t w, uint32_t h);

  // Clamp pitch and mark view dirty
  void setYawPitch(float yaw, float pitch);

  // Recompute if needed (cheap when not dirty)
  void updateIfDirty() const;

private:
  mutable bool m_viewDirty = true;
  mutable bool m_projDirty = true;

  mutable glm::mat4 m_view{1.0f};
  mutable glm::mat4 m_proj{1.0f};
  mutable glm::mat4 m_viewProj{1.0f};
};

} // namespace Nyx
