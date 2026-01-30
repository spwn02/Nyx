#pragma once

#include <glm/glm.hpp>

namespace Nyx {

class CameraFrameOverlay final {
public:
  void draw(const glm::vec2 &imgMin, const glm::vec2 &imgMax, bool enabled);
};

} // namespace Nyx
