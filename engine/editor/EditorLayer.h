#pragma once

#include "../layers/Layer.h"
#include "editor/AddMenu.h"
#include "editor/EditorPersist.h"
#include "editor/GizmoState.h"
#include "editor/HierarchyPanel.h"
#include "editor/InspectorPanel.h"
#include "editor/LockCameraToView.h"
#include "editor/Selection.h"
#include "editor/ViewportState.h"
#include "scene/EntityID.h"
#include "scene/World.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace Nyx {

class EngineContext;

struct EditorCameraController final {
  glm::vec3 position{0.0f, 1.5f, 3.0f};
  float yawDeg = -90.0f;
  float pitchDeg = 0.0f;

  float fovYDeg = 60.0f;
  float nearZ = 0.01f;
  float farZ = 2000.0f;

  float speed = 6.0f;
  float boostMul = 2.0f;
  float sensitivity = 0.12f;

  bool mouseCaptured = false;
};

class EditorLayer final : public Layer {
public:
  void onAttach() override;
  void onDetach() override;
  void onImGui(EngineContext &engine) override;

  void setViewportTexture(uint32_t tex) { m_viewportTex = tex; }

  ViewportState &viewport() { return m_viewport; }

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

  void setViewThroughCamera(bool enabled) { m_viewThroughCamera = enabled; }
  bool viewThroughCamera() const { return m_viewThroughCamera; }

  void setCameraEntity(EntityID e) { m_cameraEntity = e; }
  EntityID cameraEntity() const { return m_cameraEntity; }

  Selection &selection() { return m_sel; }
  const Selection &selection() const { return m_sel; }

  bool gizmoWantsMouse() const { return m_gizmoUsing || m_gizmoOver; }

  EditorPersistState &persist() { return m_persist; }
  const EditorPersistState &persist() const { return m_persist; }

  const EditorCameraController &cameraController() const {
    return m_cameraCtrl;
  }
  EditorCameraController &cameraController() { return m_cameraCtrl; }
  GizmoState &gizmo() { return m_gizmo; }
  const GizmoState &gizmo() const { return m_gizmo; }
  LockCameraToView &lockCameraToView() { return m_lockCam; }
  const LockCameraToView &lockCameraToView() const { return m_lockCam; }

private:
  void drawViewport(EngineContext &engine);
  void drawStats(EngineContext &engine);
  void processWorldEvents();

private:
  ViewportState m_viewport;
  uint32_t m_viewportTex = 0;
  EntityID m_selected = InvalidEntity;
  EditorPersistState m_persist{};
  EditorCameraController m_cameraCtrl{};
  GizmoState m_gizmo{};
  bool m_gizmoUsing = false;
  bool m_gizmoOver = false;
  LockCameraToView m_lockCam{};

  World *m_world = nullptr;
  EntityID m_cameraEntity = InvalidEntity;
  Selection m_sel{};

  HierarchyPanel m_hierarchy{};
  AddMenu m_add{};
  InspectorPanel m_inspector{};

  std::string m_scenePath;
  bool m_autoSave = false;
  bool m_sceneLoaded = false;
  bool m_openScenePopup = false;
  bool m_saveScenePopup = false;
  char m_scenePathBuf[512]{};
  bool m_viewThroughCamera = false;
};

} // namespace Nyx
