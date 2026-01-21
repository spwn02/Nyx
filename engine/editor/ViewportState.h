#pragma once

#include "glm/glm.hpp"
#include <cstdint>

namespace Nyx {

struct ViewportState {
  bool hovered = false;
  bool focused = false;

  glm::uvec2 desiredSize{1, 1}; // size of ImGui image in pixels (clamped >= 1)
  glm::uvec2 lastRenderedSize{1, 1}; // what renderer actually produced

  // Screen-space rect of the rendered image inside ImGui (pixels)
  glm::vec2 imageMin{0.0f};
  glm::vec2 imageMax{0.0f};

  bool hasImageRect() const {
    return imageMax.x > imageMin.x && imageMax.y > imageMin.y;
  }
};

} // namespace Nyx
