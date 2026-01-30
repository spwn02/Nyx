#pragma once

#include "scene/World.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Nyx {

struct GpuLight {
  glm::vec4 posRadius;
  glm::vec4 dirType;
  glm::vec4 colorIntensity;
  glm::vec4 spotParams;
  glm::uvec4 shadow;
};

class LightSystem final {
public:
  void initGL();
  void shutdownGL();

  void updateFromWorld(const World &world, const class ShadowSystem *shadows);

  uint32_t ssbo() const { return m_ssbo; }
  uint32_t lightCount() const { return m_lightCount; }

private:
  uint32_t m_ssbo = 0;
  uint32_t m_lightCount = 0;
  std::vector<GpuLight> m_cpuLights;
};

} // namespace Nyx
