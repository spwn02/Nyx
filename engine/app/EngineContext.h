#pragma once

#include "imgui.h"
#include "material/MaterialSystem.h"
#include "render/LightSystem.h"
#include "render/Renderer.h"
#include "render/ShadowSystem.h"
#include "render/ViewMode.h"
#include "render/shadows/CSMUtil.h"
#include "scene/CameraSystem.h"
#include "scene/EntityID.h"
#include "scene/RenderableRegistry.h"
#include "scene/World.h"
#include <cstdint>
#include <unordered_map>
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
  void setHiddenEntity(EntityID e) { m_hiddenEntity = e; }
  bool isEntityHidden(EntityID e) const {
    return (m_hiddenEntity != InvalidEntity && m_hiddenEntity == e);
  }

  uint32_t render(uint32_t windowWidth, uint32_t windowHeight,
                  uint32_t viewportWidth, uint32_t viewportHeight,
                  uint32_t fbWidth, uint32_t fbHeight, bool editorVisible);

  EntityID resolveEntityIndex(uint32_t index) const;

  MaterialSystem &materials() { return m_materials; }
  const MaterialSystem &materials() const { return m_materials; }

  ViewMode viewMode() const { return m_viewMode; }
  void setViewMode(ViewMode vm) { m_viewMode = vm; }

  void setRenderCameraOverride(EntityID cam) { m_renderCameraOverride = cam; }
  void setShadowDirViewProj(const glm::mat4 &m) { m_shadowDirViewProj = m; }
  const glm::mat4 &shadowDirViewProj() const { return m_shadowDirViewProj; }
  void setCachedCSM(const CSMResult &csm) { m_cachedCSM = csm; }
  const CSMResult &cachedCSM() const { return m_cachedCSM; }
  void setCameraCache(const glm::mat4 &view, const glm::mat4 &proj, float nearZ,
                      float farZ) {
    m_cachedView = view;
    m_cachedProj = proj;
    m_cachedNear = nearZ;
    m_cachedFar = farZ;
  }
  const glm::mat4 &cachedCameraView() const { return m_cachedView; }
  const glm::mat4 &cachedCameraProj() const { return m_cachedProj; }
  float cachedCameraNear() const { return m_cachedNear; }
  float cachedCameraFar() const { return m_cachedFar; }

  void setDockspaceID(ImGuiID id) { m_dockspaceID = id; }
  ImGuiID dockspaceID() const { return m_dockspaceID; }

  LightSystem &lights() { return m_lights; }
  const LightSystem &lights() const { return m_lights; }
  ShadowSystem &shadows() { return m_shadows; }
  const ShadowSystem &shadows() const { return m_shadows; }

  uint32_t materialIndex(const Renderable &r);
  void rebuildRenderables();
  void rebuildEntityIndexMap();

private:
  void buildRenderables();
  void handleWorldEvent(const WorldEvent &e);

private:
  float m_time = 0.0f;
  float m_dt = 0.016f;
  Renderer m_renderer{};
  MaterialSystem m_materials{};
  LightSystem m_lights{};
  ShadowSystem m_shadows{};
  CameraSystem m_cameras{};

  World m_world{};
  std::unordered_map<uint32_t, EntityID> m_entityByIndex;
  RenderableRegistry m_renderables{};
  std::vector<EntityID> m_selected{};
  std::vector<uint32_t> m_selectedPickIDs{};

  bool m_pickRequested = false;
  uint32_t m_pickX = 0;
  uint32_t m_pickY = 0;
  uint32_t m_lastPickedID = 0;
  uint32_t m_frameIndex = 0;
  uint32_t m_lastFbWidth = 0;
  uint32_t m_lastFbHeight = 0;
  EntityID m_renderCameraOverride = InvalidEntity;
  EntityID m_hiddenEntity = InvalidEntity;
  glm::mat4 m_shadowDirViewProj{1.0f};
  CSMResult m_cachedCSM{};
  glm::mat4 m_cachedView{1.0f};
  glm::mat4 m_cachedProj{1.0f};
  float m_cachedNear = 0.01f;
  float m_cachedFar = 2000.0f;

  ViewMode m_viewMode = ViewMode::Lit;
  ImGuiID m_dockspaceID = 0;
};

} // namespace Nyx
