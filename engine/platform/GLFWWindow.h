#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct GLFWwindow;

namespace Nyx {

class InputSystem;

struct WindowDesc {
  int32_t width = 1600;
  int32_t height = 900;
  std::string title = "Nyx Engine";
  bool vsync = true;
};

class GLFWWindow final {
public:
  explicit GLFWWindow(const WindowDesc &desc);
  ~GLFWWindow();

  GLFWWindow(const GLFWWindow &) = delete;
  GLFWWindow &operator=(const GLFWWindow &) = delete;

  void pollEvents();
  void swapBuffers();

  bool shouldClose() const;

  int32_t width() const { return m_width; }
  int32_t height() const { return m_height; }
  GLFWwindow *handle() const { return m_window; }

  InputSystem &input() { return *m_input; }
  const InputSystem &input() const { return *m_input; }

private:
  void initGLFW();
  void createWindow(const WindowDesc &desc);
  void initGL();

  void installInputCallbacks();

private:
  std::unique_ptr<InputSystem> m_input;
  GLFWwindow *m_window = nullptr;
  int32_t m_width = 1;
  int32_t m_height = 1;
};

} // namespace Nyx
