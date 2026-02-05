#pragma once

#include "env/EnvironmentIBL.h"
#include "imgui.h"
#include "post/FilterGraph.h"
#include "post/FilterRegistry.h"
#include "post/PostGraph.h"
#include "render/LightSystem.h"
#include "render/Renderer.h"
#include "render/ShadowDebugMode.h"
#include "render/SkyConstants.h"
#include "render/ViewMode.h"
#include "render/filters/FilterStackSSBO.h"
#include "render/gl/GLShaderUtil.h"
#include "render/material/MaterialSystem.h"
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
  float time() const { return m_time; }

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

  ShadowDebugMode shadowDebugMode() const { return m_shadowDebugMode; }
  void setShadowDebugMode(ShadowDebugMode mode) { m_shadowDebugMode = mode; }
  float shadowDebugAlpha() const { return m_shadowDebugAlpha; }
  void setShadowDebugAlpha(float alpha) { m_shadowDebugAlpha = alpha; }

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

  ShadowCSMConfig &shadowCSMConfig();
  const ShadowCSMConfig &shadowCSMConfig() const;

  void setPreviewMaterial(MaterialHandle h) { m_previewMaterial = h; }
  MaterialHandle previewMaterial() const { return m_previewMaterial; }
  glm::vec3 &previewLightDir() { return m_previewLightDir; }
  const glm::vec3 &previewLightDir() const { return m_previewLightDir; }
  glm::vec3 &previewLightColor() { return m_previewLightColor; }
  const glm::vec3 &previewLightColor() const { return m_previewLightColor; }
  float &previewLightIntensity() { return m_previewLightIntensity; }
  float previewLightIntensity() const { return m_previewLightIntensity; }
  float &previewLightExposure() { return m_previewLightExposure; }
  float previewLightExposure() const { return m_previewLightExposure; }
  float &previewAmbient() { return m_previewAmbient; }
  float previewAmbient() const { return m_previewAmbient; }

  uint32_t materialIndex(const Renderable &r);
  void rebuildRenderables();
  void rebuildEntityIndexMap();
  void resetMaterials();

  EnvironmentIBL &envIBL() { return m_envIBL; }
  const EnvironmentIBL &envIBL() const { return m_envIBL; }

  uint32_t skyUBO() const { return m_skyUBO; }
  uint32_t shadowCSMUBO() const { return m_shadowCSMUBO; }
  uint32_t texRemapSSBO() const { return m_texRemapSSBO; }

  Renderer &renderer() { return m_renderer; }
  const Renderer &renderer() const { return m_renderer; }

  PostGraph &postGraph() { return m_postGraph; }
  const PostGraph &postGraph() const { return m_postGraph; }

  FilterRegistry &filterRegistry() { return m_filterRegistry; }
  const FilterRegistry &filterRegistry() const { return m_filterRegistry; }

  FilterGraph &filterGraph() { return m_filterGraph; }
  const FilterGraph &filterGraph() const { return m_filterGraph; }

  uint32_t filterStackSSBO() const { return m_filterStack.ssbo(); }
  uint32_t postFiltersSSBO() const { return m_filterStack.ssbo(); }
  uint32_t postLUT3D() const { return m_postLUT3D; }
  uint32_t postLUTCount() const { return (uint32_t)m_postLUTs.size(); }
  uint32_t postLUTTexture(uint32_t idx) const;
  uint32_t postLUTSize(uint32_t idx) const;
  const std::vector<std::string> &postLUTPaths() const {
    return m_postLUTPaths;
  }
  uint32_t ensurePostLUT3D(const std::string &path);
  bool reloadPostLUT3D(const std::string &path);
  bool clearPostLUT(uint32_t idx);

  void initPostFilters();
  void updatePostFilters();
  void syncFilterGraphFromPostGraph();
  void markPostGraphDirty() { m_postGraphDirty = true; }

private:
  void buildRenderables();
  void handleWorldEvent(const WorldEvent &e);
  void updateSkyUBO(const RenderPassContext &ctx);

private:
  float m_time = 0.0f;
  float m_dt = 0.016f;
  Renderer m_renderer{};
  MaterialSystem m_materials{};
  LightSystem m_lights{};
  CameraSystem m_cameras{};
  EnvironmentIBL m_envIBL{};

  SkyConstants m_sky{};
  uint32_t m_skyUBO = 0;
  uint32_t m_shadowCSMUBO = 0;
  uint32_t m_texRemapSSBO = 0;
  uint32_t m_postLUT3D = 0; // identity LUT (index 0)
  std::vector<uint32_t> m_postLUTs{};
  std::vector<std::string> m_postLUTPaths{};
  std::vector<uint32_t> m_postLUTSizes{};
  std::unordered_map<std::string, uint32_t> m_postLUTIndex{};

  World m_world{};
  std::unordered_map<uint32_t, EntityID> m_entityByIndex;
  RenderableRegistry m_renderables{};
  std::vector<EntityID> m_selected{};
  std::vector<uint32_t> m_selectedPickIDs{};
  PostGraph m_postGraph{};
  FilterRegistry m_filterRegistry{};
  FilterGraph m_filterGraph{};
  FilterStackSSBO m_filterStack{};
  bool m_postGraphDirty = true;

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
  ShadowDebugMode m_shadowDebugMode = ShadowDebugMode::None;
  float m_shadowDebugAlpha = 0.85f;
  ImGuiID m_dockspaceID = 0;
  MaterialHandle m_previewMaterial = InvalidMaterial;
  glm::vec3 m_previewLightDir{0.6f, 0.7f, 0.3f};
  glm::vec3 m_previewLightColor{1.0f, 1.0f, 1.0f};
  float m_previewLightIntensity = 1.5f;
  float m_previewLightExposure = 0.0f;
  float m_previewAmbient = 0.03f;
};

} // namespace Nyx
