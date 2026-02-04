#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <imgui.h>

namespace Nyx {

struct ViewportProjector final {
  glm::mat4 viewProj{1.0f};
  ImVec2 imageMin{0, 0};
  ImVec2 imageMax{1, 1};
  uint32_t fbWidth = 1;
  uint32_t fbHeight = 1;

  bool project(const glm::vec3 &pWorld, ImVec2 &out) const {
    const glm::vec4 clip = viewProj * glm::vec4(pWorld, 1.0f);
    if (clip.w <= 0.00001f)
      return false;

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f)
      return false;

    const float u = ndc.x * 0.5f + 0.5f;
    const float v = 1.0f - (ndc.y * 0.5f + 0.5f);

    const float x = imageMin.x + u * (imageMax.x - imageMin.x);
    const float y = imageMin.y + v * (imageMax.y - imageMin.y);
    out = ImVec2(x, y);
    return true;
  }
};

} // namespace Nyx
