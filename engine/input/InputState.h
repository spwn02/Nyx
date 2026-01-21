#pragma once

#include "KeyCodes.h"
#include <array>
#include <cstdint>

namespace Nyx {

struct InputState {
  static constexpr uint32_t MaxKeys = 512;

  std::array<uint8_t, MaxKeys> down{};     // current raw
  std::array<uint8_t, MaxKeys> pressed{};  // edge this frame
  std::array<uint8_t, MaxKeys> released{}; // edge this frame

  double mouseX = 0.0;
  double mouseY = 0.0;
  double mouseDeltaX = 0.0;
  double mouseDeltaY = 0.0;

  void clearEdges() {
    pressed.fill(0);
    released.fill(0);
    mouseDeltaX = 0.0;
    mouseDeltaY = 0.0;
  }

  static constexpr uint32_t idx(Key k) {
    return static_cast<uint32_t>(k);
  }
};

} // namespace Nyx

