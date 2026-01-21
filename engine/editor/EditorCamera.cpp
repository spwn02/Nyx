#include "EditorCamera.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/fwd.hpp"
#include "glm/geometric.hpp"
#include "glm/trigonometric.hpp"
#include <algorithm>

namespace Nyx {

glm::vec3 EditorCamera::front() const {
  const float yaw = glm::radians(yawDeg);
  const float pit = glm::radians(pitchDeg);
  glm::vec3 f{
      cos(yaw) * cos(pit),
      sin(pit),
      sin(yaw) * cos(pit),
  };
  return glm::normalize(f);
}

glm::vec3 EditorCamera::right() const {
  return glm::normalize(glm::cross(front(), up()));
}

void EditorCamera::markViewDirty() {
  m_viewDirty = true;
}
void EditorCamera::markProjDirty() {
  m_projDirty = true;
}

void EditorCamera::setViewport(uint32_t w, uint32_t h) {
  w = w == 0 ? 1u : w;
  h = h == 0 ? 1u : h;
  if (viewportW != w || viewportH != h) {
    viewportW = w;
    viewportH = h;
    markProjDirty();
  }
}

void EditorCamera::setYawPitch(float yaw, float pitch) {
  yawDeg = yaw;
  pitchDeg = std::clamp(pitch, -120.0f, 120.0f);
  markViewDirty();
}

void EditorCamera::updateIfDirty() const {
  bool changed = m_viewDirty || m_projDirty;

  if (m_viewDirty) {
    const glm::vec3 f = front();
    m_view = glm::lookAt(position, position + f, up());
    m_viewDirty = false;
  }

  if (m_projDirty) {
    const float aspect = (viewportH > 0) ? (float(viewportW) / float(viewportH)) : 1.0f;
    float fov = fovYDeg;
    if (!(fov > 1.0f && fov < 179.0f))
      fov = 60.0f;
    float n = nearZ > 0.0001f ? nearZ : 0.01f;
    float f = farZ > (n + 0.1f) ? farZ : (n + 1000.0f);
    m_proj = glm::perspective(glm::radians(fov), aspect, n, f);
    m_projDirty = false;
  }

  if (changed) {
    m_viewProj = m_proj * m_view;
  }
}

} // namespace Nyx
