#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace Nyx {

struct ViewportImageRect final {
  glm::vec2 imageMin{0, 0};
  glm::vec2 imageMax{0, 0};
  glm::uvec2 renderedSize{1, 1};
};

struct ViewportPickResult final {
  bool inside = false;
  uint32_t px = 0;
  uint32_t py = 0;
  float u = 0.0f;
  float v = 0.0f;
};

ViewportPickResult mapMouseToFramebufferPixel(double mouseX, double mouseY,
                                              const ViewportImageRect &r);

} // namespace Nyx
