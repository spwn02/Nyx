#pragma once

#include "editor/Selection.h"
#include "editor/EditorState.h"
#include "input/Keybinds.h"
#include "project/ProjectManager.h"
#include "scene/EntityID.h"
#include "scene/SceneManager.h"
#include <memory>
#include <vector>

namespace Nyx {

class AppContext;
class EngineContext;

class Application final {
public:
  Application(std::unique_ptr<AppContext> app,
              std::unique_ptr<EngineContext> engine);
  ~Application();

  int run();

private:
  std::unique_ptr<AppContext> m_app;
  std::unique_ptr<EngineContext> m_engine;

  bool m_pendingViewportPick = false;
  bool m_pendingPickCtrl = false;
  bool m_pendingPickShift = false;
  EditorState m_editorState{};
  ProjectManager m_projectManager{};
  SceneManager m_sceneManager{};
  bool m_requestClose = false;
  KeybindManager m_keybinds{};
  std::vector<uint32_t> m_selectedPicksScratch{};

private:
  void setupKeybinds();
  void initializeProjectAndSceneBindings();
  void tryLoadInitialScene(bool &loadedScene);
  bool handleWindowCloseRequests(bool &openUnsavedQuitPopup);
  void renderEditorOverlay(bool &openUnsavedQuitPopup, float &projectSaveTimer);
  void processInteractiveUpdate(float dt);
  void renderAndPresentFrame();
  void finalizeAndShutdown();
};

} // namespace Nyx
