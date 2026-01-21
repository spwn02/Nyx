#pragma once

#include "../layers/Layer.h"
#include "editor/AddMenu.h"
#include "editor/EditorCamera.h"
#include "editor/EditorPersist.h"
#include "editor/GizmoState.h"
#include "editor/HierarchyPanel.h"
#include "editor/InspectorPanel.h"
#include "editor/Selection.h"
#include "editor/ViewportState.h"
#include "scene/EntityID.h"
#include "scene/World.h"
#include <cstdint>

namespace Nyx {

class EngineContext;

class EditorLayer final : public Layer {
public:
  void onAttach() override;
  void onDetach() override;
  void onImGui(EngineContext &engine) override;

  void setViewportTexture(uint32_t tex) { m_viewportTex = tex; }

  ViewportState &viewport() { return m_viewport; }

  EntityID selectedEntity() const { return m_selected; }
  void setSelectedEntity(EntityID id) { m_selected = id; }

  const World *world() const { return m_world; }
  World *world() { return m_world; }
  void setWorld(World *world) { m_world = world; }

  Selection &selection() { return m_sel; }
  const Selection &selection() const { return m_sel; }

  bool gizmoWantsMouse() const { return m_gizmoUsing || m_gizmoOver; }

  EditorPersistState &persist() { return m_persist; }
  const EditorPersistState &persist() const { return m_persist; }

  void setViewportSize(uint32_t w, uint32_t h) {
    m_persist.camera.setViewport(w, h);
  }

  const EditorCamera &camera() const { return m_persist.camera; }
  EditorCamera &camera() { return m_persist.camera; }

private:
  void drawViewport(EngineContext &engine);
  void drawStats(EngineContext &engine);

private:
  ViewportState m_viewport;
  uint32_t m_viewportTex = 0;
  EntityID m_selected = InvalidEntity;
  EditorPersistState m_persist{};
  GizmoState m_gizmo{};
  bool m_gizmoUsing = false;
  bool m_gizmoOver = false;

  World *m_world = nullptr;
  Selection m_sel{};

  HierarchyPanel m_hierarchy{};
  AddMenu m_add{};
  InspectorPanel m_inspector{};
};

} // namespace Nyx
