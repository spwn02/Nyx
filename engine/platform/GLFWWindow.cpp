#include "GLFWWindow.h"

#include "../core/Assert.h"
#include "../core/Log.h"
#include "../input/InputSystem.h"

#include <glad/glad.h>

#include <GLFW/glfw3.h>

namespace Nyx {

static void glfwErrorCallback(int code, const char *msg) {
  Log::Error("GLFW error {}: {}", code, msg ? msg : "(null)");
}

GLFWWindow::GLFWWindow(const WindowDesc &desc) {
  initGLFW();
  createWindow(desc);
  initGL();

  if (desc.vsync)
    glfwSwapInterval(1);
  else
    glfwSwapInterval(0);

  Log::Info("Window created: {}x{}", m_width, m_height);
}

GLFWWindow::~GLFWWindow() {
  if (m_window) {
    glfwDestroyWindow(m_window);
    m_window = nullptr;
  }
  glfwTerminate();
}

void GLFWWindow::initGLFW() {
  glfwSetErrorCallback(glfwErrorCallback);
  NYX_ASSERT(glfwInit() == GLFW_TRUE, "glfwInit failed");

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#if defined(__APPLE__)
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

  glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
  glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
}

void GLFWWindow::createWindow(const WindowDesc &desc) {
  m_width = desc.width;
  m_height = desc.height;

  m_window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(),
                              nullptr, nullptr);
  NYX_ASSERT(m_window != nullptr, "glfwCreateWindow failed");

  glfwMakeContextCurrent(m_window);

  glfwSetWindowUserPointer(m_window, this);

  glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow *w, int fbW, int fbH) {
    auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
    self->m_width = fbW > 0 ? fbW : 1;
    self->m_height = fbH > 0 ? fbH : 1;
    glViewport(0, 0, self->m_width, self->m_height);
  });

  int fbW = 0, fbH = 0;
  glfwGetFramebufferSize(m_window, &fbW, &fbH);
  m_width = fbW > 0 ? fbW : 1;
  m_height = fbH > 0 ? fbH : 1;
}

void GLFWWindow::initGL() {
  NYX_ASSERT(
      gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) != 0,
      "gladLoadGLLoader failed");

  Log::Info("OpenGL: {}",
            reinterpret_cast<const char *>(glGetString(GL_VERSION)));
  Log::Info("Renderer: {}",
            reinterpret_cast<const char *>(glGetString(GL_RENDERER)));

  // glEnable(GL_FRAMEBUFFER_SRGB);
  glViewport(0, 0, m_width, m_height);

  // basic debug state for now
  glEnable(GL_DEPTH_TEST);

  m_input = std::make_unique<InputSystem>(m_window);
  installInputCallbacks();
}

void GLFWWindow::pollEvents() { glfwPollEvents(); }

void GLFWWindow::waitEventsTimeout(double seconds) {
  glfwWaitEventsTimeout(seconds);
}

void GLFWWindow::swapBuffers() { glfwSwapBuffers(m_window); }

bool GLFWWindow::shouldClose() const {
  return glfwWindowShouldClose(m_window) == GLFW_TRUE;
}

void GLFWWindow::requestClose() const {
  glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void GLFWWindow::cancelCloseRequest() const {
  glfwSetWindowShouldClose(m_window, GLFW_FALSE);
}

bool GLFWWindow::isFocused() const {
  return glfwGetWindowAttrib(m_window, GLFW_FOCUSED) == GLFW_TRUE;
}

bool GLFWWindow::isVisible() const {
  return glfwGetWindowAttrib(m_window, GLFW_VISIBLE) == GLFW_TRUE;
}

bool GLFWWindow::isMinimized() const {
  return glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) == GLFW_TRUE;
}

void GLFWWindow::installInputCallbacks() {
  glfwSetKeyCallback(
      m_window, [](GLFWwindow *w, int key, int scancode, int action, int mods) {
        (void)scancode;
        (void)mods;
        auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        if (!self)
          return;
        self->m_input->onKey(key, action);
      });

  glfwSetMouseButtonCallback(
      m_window, [](GLFWwindow *w, int button, int action, int mods) {
        (void)mods;
        auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        if (!self)
          return;
        self->m_input->onMouseButton(button, action);
      });

  glfwSetCursorPosCallback(m_window, [](GLFWwindow *w, double x, double y) {
    auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
    if (!self)
      return;
    self->m_input->onCursorPos(x, y);
  });

  glfwSetScrollCallback(
      m_window, [](GLFWwindow *w, double xoffset, double yoffset) {
        auto *self = static_cast<GLFWWindow *>(glfwGetWindowUserPointer(w));
        if (!self)
          return;
        self->m_input->onScroll(xoffset, yoffset);
      });
}

void GLFWWindow::disableCursor(bool disabled) const {
  if (disabled) {
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  } else {
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }
}

double GLFWWindow::getTimeSeconds() const { return glfwGetTime(); }

} // namespace Nyx
