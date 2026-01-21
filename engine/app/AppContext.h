#pragma once

#include "editor/EditorLayer.h"
#include "layers/LayerStack.h"
#include <memory>

struct GLFWwindow;

namespace Nyx {

class GLFWWindow;

class AppContext final {
public:
  explicit AppContext(std::unique_ptr<GLFWWindow> window);
  ~AppContext();

  AppContext(const AppContext &) = delete;
  AppContext &operator=(const AppContext &) = delete;

  GLFWWindow &window();
  LayerStack &layers() { return m_layers; }

  void beginFrame();
  void endFrame();

  void imguiBegin();
  void imguiEnd();

  void toggleEditorOverlay();
  bool isEditorVisible() const { return m_editorVisible; }
  EditorLayer *editorLayer() { return m_editorLayer; }

private:
  void initImGui();
  void shutdownImGui();

private:
  std::unique_ptr<GLFWWindow> m_window;
  LayerStack m_layers;
  EditorLayer *m_editorLayer = nullptr;
  bool m_editorVisible = false;
};

} // namespace Nyx
