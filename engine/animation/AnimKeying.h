#pragma once

#include "AnimNLA.h"
#include "scene/EntityID.h"
#include <cstdint>

namespace Nyx {

enum class KeyingMode : uint8_t {
  Replace = 0,
  Add,
};

struct KeyingSettings final {
  bool autoKey = false;
  bool keyTranslate = true;
  bool keyRotate = true;
  bool keyScale = true;
  KeyingMode mode = KeyingMode::Replace;
};

struct KeyingTarget final {
  ActionID action = 0;
  EntityID restrictEntity = InvalidEntity;
};

void keyValue(AnimAction &a, AnimChannel ch, AnimFrame frame, float value,
              KeyingMode mode);

} // namespace Nyx

