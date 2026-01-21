#pragma once

#include <cstdint>

namespace Nyx {

enum class Key : uint16_t {
  Unknown = 0,
  F,
  Escape,
  W,
  A,
  S,
  D,
  Q,
  E,
  X,
  Space,
  Delete,
  LeftShift,
  RightShift,
  LeftCtrl,
  RightCtrl,
  LeftAlt,
  RightAlt,
  MouseLeft,
  MouseRight,
  MouseMiddle, // treat as "keys" for now
};

} // namespace Nyx
