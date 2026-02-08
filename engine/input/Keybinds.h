#pragma once

#include "InputSystem.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Nyx {

enum class KeyMod : uint8_t {
  None = 0,
  Ctrl = 1 << 0,
  Shift = 1 << 1,
  Alt = 1 << 2,
};

inline KeyMod operator|(KeyMod a, KeyMod b) {
  return (KeyMod)((uint8_t)a | (uint8_t)b);
}

inline bool hasMod(KeyMod set, KeyMod v) {
  return ((uint8_t)set & (uint8_t)v) != 0;
}

struct KeyChord {
  std::vector<Key> keys; // non-modifier keys
  KeyMod mods = KeyMod::None;
  bool allowExtraKeys = true; // also controls extra modifiers
  bool triggerOnPress = true;
  std::vector<Key> extraAllowed; // allowed extra keys when allowExtraKeys=false
};

struct Keybind {
  std::string id;
  KeyChord chord;
  int priority = 0;
  bool consume = true;
  std::function<bool()> enabled;
  std::function<void()> action;
};

class KeybindManager final {
public:
  void add(Keybind kb);
  void clear();
  bool process(const InputSystem &input) const;

private:
  std::vector<Keybind> m_binds;
};

} // namespace Nyx

