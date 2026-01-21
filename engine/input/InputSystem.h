#pragma once

#include "../core/Assert.h"
#include "InputState.h"

struct GLFWwindow;

namespace Nyx {

class InputSystem final {
public:
  explicit InputSystem(GLFWwindow *window);

  void beginFrame(); // clears edges + prepares delta
  void endFrame();   // (unused now, reserved)

  const InputState &state() const { return m_state; }

  bool isDown(Key k) const { return m_state.down[InputState::idx(k)] != 0; }
  bool isPressed(Key k) const {
    return m_state.pressed[InputState::idx(k)] != 0;
  }
  bool isReleased(Key k) const {
    return m_state.released[InputState::idx(k)] != 0;
  }

  void onKey(int key, int action);
  void onMouseButton(int button, int action);
  void onCursorPos(double x, double y);

private:
  void installCallbacks();

  static void cbKey(GLFWwindow *w, int key, int scancode, int action, int mods);
  static void cbMouseButton(GLFWwindow *w, int button, int action, int mods);
  static void cbCursorPos(GLFWwindow *w, double x, double y);
  static Key mapGLFWKey(int key);
  static Key mapGLFWMouseButton(int button);

private:
  GLFWwindow *m_window = nullptr;
  InputState m_state{};
  bool m_hasMouse = false;
  double m_lastMouseX = 0.0;
  double m_lastMouseY = 0.0;
};

} // namespace Nyx
