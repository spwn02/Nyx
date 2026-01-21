#include "AppContext.h"

#include "../core/Assert.h"
#include "../core/Log.h"
#include "../platform/GLFWWindow.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

namespace Nyx {

AppContext::AppContext(std::unique_ptr<GLFWWindow> window)
    : m_window(std::move(window)) {
  NYX_ASSERT(m_window != nullptr, "AppContext requires a window");
  initImGui();
  toggleEditorOverlay();
}

AppContext::~AppContext() { shutdownImGui(); }

GLFWWindow &AppContext::window() { return *m_window; }

void AppContext::beginFrame() { m_window->pollEvents(); }

void AppContext::endFrame() { m_window->swapBuffers(); }

void AppContext::initImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  ImGui::StyleColorsDark();

  // backend
  GLFWwindow *w = m_window->handle();
  NYX_ASSERT(ImGui_ImplGlfw_InitForOpenGL(w, true),
             "ImGui_ImplGlfw_InitForOpenGL failed");
  NYX_ASSERT(ImGui_ImplOpenGL3_Init("#version 460 core"),
             "ImGui_ImplOpenGL3_Init failed");

  const bool viewports_supported =
      (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) != 0;
  if (viewports_supported)
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

  if (viewports_supported)
    Log::Info("ImGui initialized (Docking + Viewports)");
  else
    Log::Info("ImGui initialized (Docking; platform viewports unsupported)");
}

void AppContext::shutdownImGui() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void AppContext::imguiBegin() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, m_window->width(), m_window->height());

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void AppContext::imguiEnd() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, m_window->width(), m_window->height());

  ImGuiIO &io = ImGui::GetIO();
  (void)io;

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  // Multi-viewport support
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    GLFWwindow *backup = glfwGetCurrentContext();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    glfwMakeContextCurrent(backup);
  }
}

void AppContext::toggleEditorOverlay() {
  if (m_editorVisible) {
    if (m_editorLayer) {
      m_layers.popLayer(m_editorLayer);
      m_editorLayer = nullptr;
    }
    m_editorVisible = false;
    return;
  }

  auto layer = std::make_unique<EditorLayer>();
  m_editorLayer = layer.get();
  m_layers.pushLayer(std::move(layer));
  m_editorVisible = true;
}

} // namespace Nyx
