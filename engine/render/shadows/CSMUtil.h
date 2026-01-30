#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace Nyx {

struct CSMSettings final {
  int cascades = 4;
  float nearPlane = 0.01f;
  float farPlane = 200.0f;
  float lambda = 0.7f;
  uint32_t mapSize = 2048;
  float orthoPadding = 10.0f;
  bool stabilize = true;
  float polyOffsetFactor = 2.0f;
  float polyOffsetUnits = 4.0f;
};

struct CSMSlice final {
  float splitNear = 0.0f;
  float splitFar = 0.0f;
  glm::mat4 lightViewProj{1.0f};
};

struct CSMResult final {
  std::array<CSMSlice, 4> slices{};
  glm::vec4 splitFar{1, 1, 1, 1};
};

CSMResult buildCSM(const CSMSettings &s, const glm::mat4 &camView,
                   const glm::mat4 &camProj, const glm::vec3 &lightDirWS);

} // namespace Nyx
