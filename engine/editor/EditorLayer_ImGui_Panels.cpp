#include "EditorLayer.h"

#include "app/EngineContext.h"
#include "material/MaterialHandle.h"
#include "scene/Pick.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <imgui.h>

namespace Nyx {

void EditorLayer::drawProjectAndSceneBrowsers(EngineContext &engine) {
  if (m_projectManager) {
    m_projectPanel.draw(*m_projectManager);
    m_projectBrowserPanel.draw(*m_projectManager);
  }

  if (!(m_projectManager && m_sceneManager))
    return;

  m_sceneBrowserPanel.draw(*m_sceneManager, *m_projectManager);
  if (m_sceneManager->hasActive() && m_scenePath != m_sceneManager->active().pathAbs) {
    m_scenePath = m_sceneManager->active().pathAbs;
    m_sceneLoaded = true;
    markSceneClean(engine);
    m_sel.clear();
    m_hierarchy.setWorld(m_world);
    engine.rebuildEntityIndexMap();
    engine.rebuildRenderables();
    const auto &sky = m_world->skySettings();
    if (!sky.hdriPath.empty())
      engine.envIBL().loadFromHDR(sky.hdriPath);
  }
}

bool EditorLayer::drawNoWorldFallback() {
  if (m_world)
    return false;
  ImGui::Begin("Hierarchy");
  ImGui::TextUnformatted("No world loaded");
  ImGui::End();
  return true;
}

void EditorLayer::configureSequencerBindings(EngineContext &engine) {
  m_sequencerPanel.setWorld(m_world);
  m_sequencerPanel.setAnimationSystem(&engine.animation());
  m_sequencerPanel.setAnimationClip(&engine.activeClip());

  std::vector<EntityID> exclude;
  exclude.push_back(m_editorCamera);
  exclude.push_back(m_world->activeCamera());
  m_sequencerPanel.setHiddenExclusions(exclude);
  m_sequencerPanel.setTrackExclusions(exclude);
}

void EditorLayer::drawEditorPanels(EngineContext &engine) {
  if (m_persist.panels.viewport)
    m_viewport.draw(engine, *this);

  if (m_persist.panels.stats)
    drawStats(engine, m_viewport.gizmoState());

  if (m_persist.panels.projectSettings)
    m_projectSettings.draw(*this, engine);

  if (m_persist.panels.hierarchy)
    m_hierarchy.draw(*m_world, m_editorCamera, engine, m_sel);

  if (m_persist.panels.history)
    m_historyPanel.draw(m_history, *m_world, engine.materials(), m_sel, engine);

  if (m_sel.focusEntity != InvalidEntity && m_world->isAlive(m_sel.focusEntity)) {
    const glm::mat4 &w = m_world->worldTransform(m_sel.focusEntity).world;
    const glm::vec3 center = glm::vec3(w[3]);
    m_cameraCtrl.center = center;
    const float yaw = glm::radians(m_cameraCtrl.yawDeg);
    const float pitch = glm::radians(m_cameraCtrl.pitchDeg);
    glm::vec3 front;
    front.x = std::cos(yaw) * std::cos(pitch);
    front.y = std::sin(pitch);
    front.z = std::sin(yaw) * std::cos(pitch);
    front = glm::normalize(front);
    const float dist = std::max(0.1f, m_cameraCtrl.distance);
    m_cameraCtrl.position = m_cameraCtrl.center - front * dist;
    m_sel.focusEntity = InvalidEntity;
  }

  const bool allowOpen = !ImGui::GetIO().WantTextInput &&
                         !engine.uiBlockGlobalShortcuts() &&
                         !m_postGraphPanel.isHovered() &&
                         !m_materialGraphPanel.isHovered() &&
                         !m_cameraCtrl.mouseCaptured;
  m_add.tick(*m_world, m_sel, allowOpen);

  if (m_persist.panels.inspector)
    m_inspector.draw(*m_world, engine, m_sel, &m_sequencerPanel);

  if (m_persist.panels.materialGraph) {
    MaterialHandle activeMat = InvalidMaterial;
    if (m_sel.kind == SelectionKind::Material && m_sel.activeMaterial != InvalidMaterial) {
      activeMat = m_sel.activeMaterial;
    } else if (!m_sel.isEmpty()) {
      const uint32_t activePick = m_sel.activePick ? m_sel.activePick : m_sel.picks.back();
      EntityID e = m_sel.entityForPick(activePick);
      if (e == InvalidEntity)
        e = engine.resolveEntityIndex(pickEntity(activePick));
      const uint32_t sub = pickSubmesh(activePick);
      if (e != InvalidEntity && m_world->isAlive(e) && m_world->hasMesh(e) &&
          sub < m_world->submeshCount(e)) {
        activeMat = m_world->submesh(e, sub).material;
      }
    }
    m_materialGraphPanel.setMaterial(activeMat);
    m_materialGraphPanel.draw(engine);
  }

  if (m_persist.panels.sky)
    drawSkyPanel(*m_world, engine);

  if (m_autoSave && m_sceneLoaded && !m_scenePath.empty() && m_sceneManager &&
      m_sceneManager->hasActive() && m_sceneManager->active().dirty) {
    if (m_sceneManager->saveActive())
      markSceneClean(engine);
  }

  if (m_persist.panels.assetBrowser)
    m_assetBrowser.draw(&m_persist.panels.assetBrowser);

  if (m_persist.panels.lutManager)
    m_lutManager.draw(engine);

  const auto &gizmo = m_viewport.gizmoState();

  if (m_persist.panels.postGraph) {
    m_postGraphPanel.draw(engine.postGraph(), engine.filterRegistry(), engine);
    if (m_postGraphPanel.consumeGraphChanged()) {
      engine.markPostGraphDirty();
      engine.syncFilterGraphFromPostGraph();
      engine.updatePostFilters();
      storePostGraphPersist(engine);
    }
  }

  if (m_persist.panels.sequencer) {
    m_sequencerPanel.draw();
    if (m_sequencerPanel.timelineHot())
      engine.requestUiBlockGlobalShortcuts();
    engine.setHiddenEntities(m_sequencerPanel.hiddenEntities());
  }

  m_persist.gizmoOp = gizmo.op;
  m_persist.gizmoMode = gizmo.mode;
  m_persist.gizmoUseSnap = gizmo.useSnap;
  m_persist.gizmoSnapTranslate = gizmo.snapTranslate;
  m_persist.gizmoSnapRotateDeg = gizmo.snapRotateDeg;
  m_persist.gizmoSnapScale = gizmo.snapScale;
}

} // namespace Nyx
