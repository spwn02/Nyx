#include "InputSystem.h"

#include <GLFW/glfw3.h>

namespace Nyx {

InputSystem::InputSystem(GLFWwindow *window) : m_window(window) {
  NYX_ASSERT(m_window != nullptr, "InputSystem requires GLFWwindow*");
  glfwSetWindowUserPointer(
      m_window, glfwGetWindowUserPointer(m_window)); // keep existing user ptr
  installCallbacks();
}

void InputSystem::beginFrame() { m_state.clearEdges(); }

void InputSystem::endFrame() {}

void InputSystem::installCallbacks() {}

static InputSystem *getInput(GLFWwindow *w) { return nullptr; }

// --- mapping
Key InputSystem::mapGLFWKey(int key) {
  switch (key) {
  case GLFW_KEY_F:
    return Key::F;
  case GLFW_KEY_ESCAPE:
    return Key::Escape;
  case GLFW_KEY_W:
    return Key::W;
  case GLFW_KEY_A:
    return Key::A;
  case GLFW_KEY_S:
    return Key::S;
  case GLFW_KEY_D:
    return Key::D;
  case GLFW_KEY_Q:
    return Key::Q;
  case GLFW_KEY_E:
    return Key::E;
  case GLFW_KEY_X:
    return Key::X;
  case GLFW_KEY_Z:
    return Key::Z;
  case GLFW_KEY_R:
    return Key::R;
  case GLFW_KEY_DELETE:
    return Key::Delete;
  case GLFW_KEY_SPACE:
    return Key::Space;
  case GLFW_KEY_LEFT_SHIFT:
    return Key::LeftShift;
  case GLFW_KEY_RIGHT_SHIFT:
    return Key::RightShift;
  case GLFW_KEY_LEFT_CONTROL:
    return Key::LeftCtrl;
  case GLFW_KEY_RIGHT_CONTROL:
    return Key::RightCtrl;
  case GLFW_KEY_LEFT_ALT:
    return Key::LeftAlt;
  case GLFW_KEY_RIGHT_ALT:
    return Key::RightAlt;
  case GLFW_KEY_LEFT:
    return Key::ArrowLeft;
  case GLFW_KEY_RIGHT:
    return Key::ArrowRight;
  default:
    return Key::Unknown;
  }
}

Key InputSystem::mapGLFWMouseButton(int button) {
  switch (button) {
  case GLFW_MOUSE_BUTTON_LEFT:
    return Key::MouseLeft;
  case GLFW_MOUSE_BUTTON_RIGHT:
    return Key::MouseRight;
  case GLFW_MOUSE_BUTTON_MIDDLE:
    return Key::MouseMiddle;
  default:
    return Key::Unknown;
  }
}

void InputSystem::onKey(int key, int action) {
  Key k = mapGLFWKey(key);
  if (k == Key::Unknown)
    return;

  const uint32_t i = InputState::idx(k);

  if (action == GLFW_PRESS) {
    if (!m_state.down[i])
      m_state.pressed[i] = 1;
    m_state.down[i] = 1;
  } else if (action == GLFW_RELEASE) {
    m_state.down[i] = 0;
    m_state.released[i] = 1;
  }
}

void InputSystem::onMouseButton(int button, int action) {
  Key k = mapGLFWMouseButton(button);
  if (k == Key::Unknown)
    return;

  const uint32_t i = InputState::idx(k);

  if (action == GLFW_PRESS) {
    if (!m_state.down[i])
      m_state.pressed[i] = 1;
    m_state.down[i] = 1;
  } else if (action == GLFW_RELEASE) {
    m_state.down[i] = 0;
    m_state.released[i] = 1;
  }
}

void InputSystem::onCursorPos(double x, double y) {
  m_state.mouseX = x;
  m_state.mouseY = y;

  if (!m_hasMouse) {
    m_hasMouse = true;
    m_lastMouseX = x;
    m_lastMouseY = y;
    return;
  }

  m_state.mouseDeltaX += (x - m_lastMouseX);
  m_state.mouseDeltaY += (y - m_lastMouseY);
  m_lastMouseX = x;
  m_lastMouseY = y;
}

void InputSystem::onScroll(double xoffset, double yoffset) {
  m_state.scrollX += xoffset;
  m_state.scrollY += yoffset;
}

} // namespace Nyx
