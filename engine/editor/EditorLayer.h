#pragma once

#include "Selection.h"
#include "ViewportState.h"
#include "editor/ui/panels/PostGraphEditorPanel.h"
#include "layers/Layer.h"
#include "scene/EntityID.h"
#include "scene/World.h"
#include "tools/CameraController.h"
#include "tools/EditorPersist.h"
#include "tools/LockCameraToView.h"
#include "ui/GizmoState.h"
#include "ui/panels/AddMenu.h"
#include "ui/panels/AssetBrowserPanel.h"
#include "ui/panels/HierarchyPanel.h"
#include "ui/panels/InspectorPanel.h"
#include "ui/panels/InspectorSky.h"
#include "ui/panels/LUTManagerPanel.h"
#include "ui/panels/ViewportPanel.h"
#include <cstdint>
#include <string>

namespace Nyx {

class EngineContext;

class EditorLayer final : public Layer {
public:
  void onAttach() override;
  void onDetach() override;
  void onImGui(EngineContext &engine) override;

  void setViewportTexture(uint32_t tex) { m_viewport.setViewportTexture(tex); }
  ViewportState &viewport() { return m_viewport.viewport(); }

  void syncWorldEvents();

  EntityID selectedEntity() const { return m_selected; }
  void setSelectedEntity(EntityID id) { m_selected = id; }

  const World *world() const { return m_world; }
  World *world() { return m_world; }
  void setWorld(World *world);

  void setScenePath(const std::string &path) { m_scenePath = path; }
  const std::string &scenePath() const { return m_scenePath; }

  void setAutoSave(bool enabled) { m_autoSave = enabled; }
  bool autoSave() const { return m_autoSave; }

  void setSceneLoaded(bool loaded) { m_sceneLoaded = loaded; }
  bool sceneLoaded() const { return m_sceneLoaded; }

  void defaultScene(EngineContext &engine);

  void setViewThroughCamera(bool enabled) {
    m_viewport.setViewThroughCamera(enabled);
  }
  bool viewThroughCamera() { return m_viewport.viewThroughCamera(); }

  void setCameraEntity(EntityID e) { m_cameraEntity = e; }
  EntityID cameraEntity() const { return m_cameraEntity; }
  EntityID editorCamera() const { return m_editorCamera; }

  Selection &selection() { return m_sel; }
  const Selection &selection() const { return m_sel; }

  bool gizmoWantsMouse() const { return m_viewport.gizmoWantsMouse(); }

  EditorPersistState &persist() { return m_persist; }
  const EditorPersistState &persist() const { return m_persist; }

  const EditorCameraController &cameraController() const {
    return m_cameraCtrl;
  }
  EditorCameraController &cameraController() { return m_cameraCtrl; }
  GizmoState &gizmo() { return m_viewport.gizmoState(); }
  const GizmoState &gizmo() const { return m_viewport.gizmoState(); }
  LockCameraToView &lockCameraToView() { return m_viewport.lockCameraToView(); }
  const LockCameraToView &lockCameraToView() const {
    return m_viewport.lockCameraToView();
  }

private:
  void drawStats(EngineContext &engine);
  void processWorldEvents();
  void applyPostGraphPersist(EngineContext &engine);
  void storePostGraphPersist(EngineContext &engine);

private:
  EntityID m_selected = InvalidEntity;
  EditorPersistState m_persist{};
  EditorCameraController m_cameraCtrl{};

  World *m_world = nullptr;
  EntityID m_cameraEntity = InvalidEntity;
  Selection m_sel{};
  EntityID m_editorCamera = InvalidEntity;
  HierarchyPanel m_hierarchy{};
  AddMenu m_add{};
  InspectorPanel m_inspector{};
  ViewportPanel m_viewport{};
  AssetBrowserPanel m_assetBrowser{};
  LUTManagerPanel m_lutManager{};
  PostGraphEditorPanel m_postGraphPanel{};
  bool m_postGraphLoaded = false;

  std::string m_scenePath;
  bool m_autoSave = false;
  bool m_sceneLoaded = false;
  bool m_openScenePopup = false;
  bool m_saveScenePopup = false;
  char m_scenePathBuf[512]{};
};

} // namespace Nyx
