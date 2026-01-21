#pragma once

#include "editor/EditorCamera.h"
#include "imgui.h"
#include "material/MaterialSystem.h"
#include "render/Renderer.h"
#include "render/ViewMode.h"
#include "scene/EntityID.h"
#include "scene/RenderableRegistry.h"
#include "scene/World.h"
#include <cstdint>
#include <vector>

namespace Nyx {

class EngineContext final {
public:
  EngineContext();
  ~EngineContext();

  void tick(float dt);
  float dt() const { return m_dt; }

  World &world() { return m_world; }
  const World &world() const { return m_world; }

  void requestPick(uint32_t px, uint32_t py) {
    m_pickRequested = true;
    m_pickX = px;
    m_pickY = py;
  }
  uint32_t lastPickedID() const { return m_lastPickedID; }
  void setSelection(const std::vector<EntityID> &ids) { m_selected = ids; }
  void setSelectionPickIDs(const std::vector<uint32_t> &ids) {
    m_selectedPickIDs = ids;
  }
  const std::vector<uint32_t> &selectedPickIDs() const {
    return m_selectedPickIDs;
  }

  uint32_t render(uint32_t fbWidth, uint32_t fbHeight, EditorCamera &camera,
                  bool editorVisible);

  MaterialSystem &materials() { return m_materials; }
  const MaterialSystem &materials() const { return m_materials; }

  ViewMode viewMode() const { return m_viewMode; }
  void setViewMode(ViewMode vm) { m_viewMode = vm; }

  void setDockspaceID(ImGuiID id) { m_dockspaceID = id; }
  ImGuiID dockspaceID() const { return m_dockspaceID; }

private:
  void buildRenderables();

private:
  float m_time = 0.0f;
  float m_dt = 0.016f;
  Renderer m_renderer{};
  MaterialSystem m_materials{};

  World m_world{};
  RenderableRegistry m_renderables{};
  std::vector<EntityID> m_selected{};
  std::vector<uint32_t> m_selectedPickIDs{};

  bool m_pickRequested = false;
  uint32_t m_pickX = 0;
  uint32_t m_pickY = 0;
  uint32_t m_lastPickedID = 0;

  ViewMode m_viewMode = ViewMode::Lit;
  ImGuiID m_dockspaceID = 0;
};

} // namespace Nyx
