#include "Keybinds.h"

#include <algorithm>

namespace Nyx {

static bool isModifierKey(Key k) {
  switch (k) {
  case Key::LeftShift:
  case Key::RightShift:
  case Key::LeftCtrl:
  case Key::RightCtrl:
  case Key::LeftAlt:
  case Key::RightAlt:
    return true;
  default:
    return false;
  }
}

static bool isCtrlDown(const InputSystem &input) {
  return input.isDown(Key::LeftCtrl) || input.isDown(Key::RightCtrl);
}
static bool isShiftDown(const InputSystem &input) {
  return input.isDown(Key::LeftShift) || input.isDown(Key::RightShift);
}
static bool isAltDown(const InputSystem &input) {
  return input.isDown(Key::LeftAlt) || input.isDown(Key::RightAlt);
}

static bool containsKey(const std::vector<Key> &keys, Key k) {
  for (Key v : keys) {
    if (v == k)
      return true;
  }
  return false;
}

static bool matchChord(const InputSystem &input, const KeyChord &c) {
  if (c.keys.empty())
    return false;

  const bool ctrlDown = isCtrlDown(input);
  const bool shiftDown = isShiftDown(input);
  const bool altDown = isAltDown(input);

  const bool ctrlReq = hasMod(c.mods, KeyMod::Ctrl);
  const bool shiftReq = hasMod(c.mods, KeyMod::Shift);
  const bool altReq = hasMod(c.mods, KeyMod::Alt);

  if (ctrlReq && !ctrlDown)
    return false;
  if (shiftReq && !shiftDown)
    return false;
  if (altReq && !altDown)
    return false;

  if (!c.allowExtraKeys) {
    if (!ctrlReq && ctrlDown)
      return false;
    if (!shiftReq && shiftDown)
      return false;
    if (!altReq && altDown)
      return false;
  }

  bool anyPressed = false;
  for (Key k : c.keys) {
    if (!input.isDown(k))
      return false;
    if (input.isPressed(k))
      anyPressed = true;
  }
  if (c.triggerOnPress && !anyPressed)
    return false;

  if (!c.allowExtraKeys) {
    const auto &st = input.state();
    for (uint32_t i = 0; i < InputState::MaxKeys; ++i) {
      if (!st.down[i])
        continue;
      const Key k = (Key)i;
      if (isModifierKey(k))
        continue;
      if (containsKey(c.keys, k))
        continue;
      if (containsKey(c.extraAllowed, k))
        continue;
      return false;
    }
  }

  return true;
}

void KeybindManager::add(Keybind kb) {
  m_binds.push_back(std::move(kb));
  std::stable_sort(m_binds.begin(), m_binds.end(),
                   [](const Keybind &a, const Keybind &b) {
                     return a.priority > b.priority;
                   });
}

void KeybindManager::clear() { m_binds.clear(); }

bool KeybindManager::process(const InputSystem &input) const {
  bool handled = false;
  for (const auto &kb : m_binds) {
    if (kb.enabled && !kb.enabled())
      continue;
    if (!matchChord(input, kb.chord))
      continue;
    if (kb.action)
      kb.action();
    handled = true;
    if (kb.consume)
      return true;
  }
  return handled;
}

} // namespace Nyx
