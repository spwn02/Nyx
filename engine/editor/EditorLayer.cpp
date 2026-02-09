#include "EditorLayer.h"

#include "app/EngineContext.h"
#include "core/Log.h"
#include "scene/EntityID.h"
#include "scene/Pick.h"
#include "tools/EditorPersist.h"
#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>

namespace Nyx {

static std::string editorStatePath() {
  return (std::filesystem::current_path() / ".cache" / "editor_state.ini")
      .string();
}

void EditorLayer::onAttach() {
  auto res = EditorPersist::load(editorStatePath(), m_persist);
  if (!res) {
    Log::Warn("EditorPersist load failed: {}", res.error());
  }
  m_cameraCtrl.position = m_persist.camera.position;
  m_cameraCtrl.yawDeg = m_persist.camera.yawDeg;
  m_cameraCtrl.pitchDeg = m_persist.camera.pitchDeg;
  m_cameraCtrl.fovYDeg = m_persist.camera.fovYDeg;
  m_cameraCtrl.nearZ = m_persist.camera.nearZ;
  m_cameraCtrl.farZ = m_persist.camera.farZ;
  m_cameraCtrl.speed = m_persist.camera.speed;
  m_cameraCtrl.boostMul = m_persist.camera.boostMul;
  m_cameraCtrl.sensitivity = m_persist.camera.sensitivity;

  auto &gizmo = m_viewport.gizmoState();

  gizmo.op = m_persist.gizmoOp;
  gizmo.mode = m_persist.gizmoMode;
  gizmo.useSnap = m_persist.gizmoUseSnap;
  gizmo.snapTranslate = m_persist.gizmoSnapTranslate;
  gizmo.snapRotateDeg = m_persist.gizmoSnapRotateDeg;
  gizmo.snapScale = m_persist.gizmoSnapScale;

  m_assetBrowser.setRoot(std::filesystem::current_path() / "assets");
  if (!m_persist.assetBrowserFolder.empty())
    m_assetBrowser.setCurrentFolder(m_persist.assetBrowserFolder);
  if (!m_persist.assetBrowserFilter.empty())
    m_assetBrowser.setFilter(m_persist.assetBrowserFilter);
}

void EditorLayer::onDetach() {
  m_assetBrowser.shutdown();
  m_assetBrowser.setRegistry(nullptr);
  m_assets.shutdown();
  m_assetProjectFileAbs.clear();
  m_persist.camera.position = m_cameraCtrl.position;
  m_persist.camera.yawDeg = m_cameraCtrl.yawDeg;
  m_persist.camera.pitchDeg = m_cameraCtrl.pitchDeg;
  m_persist.camera.fovYDeg = m_cameraCtrl.fovYDeg;
  m_persist.camera.nearZ = m_cameraCtrl.nearZ;
  m_persist.camera.farZ = m_cameraCtrl.farZ;
  m_persist.camera.speed = m_cameraCtrl.speed;
  m_persist.camera.boostMul = m_cameraCtrl.boostMul;
  m_persist.camera.sensitivity = m_cameraCtrl.sensitivity;

  const auto &gizmo = m_viewport.gizmoState();

  m_persist.gizmoOp = gizmo.op;
  m_persist.gizmoMode = gizmo.mode;
  m_persist.gizmoUseSnap = gizmo.useSnap;
  m_persist.gizmoSnapTranslate = gizmo.snapTranslate;
  m_persist.gizmoSnapRotateDeg = gizmo.snapRotateDeg;
  m_persist.gizmoSnapScale = gizmo.snapScale;

  m_persist.assetBrowserFolder = m_assetBrowser.currentFolder();
  m_persist.assetBrowserFilter = m_assetBrowser.filter();
  auto res = EditorPersist::save(editorStatePath(), m_persist);
  if (!res) {
    Log::Warn("EditorPersist save failed: {}", res.error());
  }
}

void EditorLayer::setWorld(World *world) {
  m_world = world;
  m_hierarchy.setWorld(world);

  if (m_editorCamera != InvalidEntity && world->isAlive(m_editorCamera) &&
      world->hasCamera(m_editorCamera) && !world->hasMesh(m_editorCamera) &&
      world->name(m_editorCamera).name == "Editor Camera") {
    setCameraEntity(m_editorCamera);
    m_cameraCtrl.apply(*world, m_editorCamera);
    return;
  }

  m_editorCamera = InvalidEntity;
  for (EntityID e : world->alive()) {
    if (!world->isAlive(e))
      continue;
    if (world->name(e).name != "Editor Camera")
      continue;
    if (!world->hasCamera(e) || world->hasMesh(e))
      continue;
    m_editorCamera = e;
    break;
  }

  if (m_editorCamera == InvalidEntity)
    m_editorCamera = world->createEntity("Editor Camera");
  if (m_editorCamera != InvalidEntity) {
    world->ensureCamera(m_editorCamera);
    if (world->activeCamera() == InvalidEntity)
      world->setActiveCamera(m_editorCamera);
    setCameraEntity(m_editorCamera);
    m_cameraCtrl.apply(*world, m_editorCamera);
  } else {
    EditorCameraController ctrl{};
    ctrl.apply(*world, m_editorCamera);
  }
}

void EditorLayer::defaultScene(EngineContext &engine) {
  if (!m_world)
    return;

  engine.resetMaterials();
  m_world->clear();
  setWorld(m_world);

  m_scenePath.clear();
  m_sceneLoaded = true;
  m_sel.clear();
  m_hierarchy.setWorld(m_world);

  EntityID cube = m_world->createEntity("Cube");
  auto &mc = m_world->ensureMesh(cube);
  if (mc.submeshes.empty())
    mc.submeshes.push_back(MeshSubmesh{.name = "Submesh 0",
                                       .type = ProcMeshType::Cube,
                                       .material = InvalidMaterial});
  mc.submeshes[0].type = ProcMeshType::Cube;

  auto &tr = m_world->transform(cube);
  tr.translation = {0.0f, 0.0f, 0.0f};
  tr.scale = {1.0f, 1.0f, 1.0f};

  EntityID cam = m_world->createEntity("Camera");
  if (cam != InvalidEntity) {
    m_world->ensureCamera(cam);
    auto &ctr = m_world->transform(cam);
    ctr.translation = {0.0f, 0.0f, 5.0f};
    ctr.scale = {1.0f, 1.0f, 1.0f};
    m_world->setActiveCamera(cam);
  }

  EntityID light = m_world->createEntity("Light");
  if (light != InvalidEntity) {
    auto &lc = m_world->ensureLight(light);
    lc.type = LightType::Point;
    lc.intensity = 25.0f;
    lc.radius = 15.0f;
    auto &ltr = m_world->transform(light);
    ltr.translation = {2.0f, 4.0f, 2.0f};
  }

  m_sel.setSinglePick(packPick(cube, 0), cube);
  m_sel.activeEntity = cube;

  engine.rebuildEntityIndexMap();
  engine.rebuildRenderables();
}

void EditorLayer::applyPostGraphPersist(EngineContext &engine) {
  if (m_persist.postGraphFilters.empty())
    return;

  PostGraph &pg = engine.postGraph();
  pg = PostGraph();

  for (const auto &n : m_persist.postGraphFilters) {
    const FilterTypeInfo *ti =
        engine.filterRegistry().find(static_cast<FilterTypeId>(n.typeId));
    if (!ti)
      continue;

    const char *label = !n.label.empty() ? n.label.c_str() : ti->name;
    std::vector<float> params = n.params;
    if (params.empty()) {
      params.reserve(ti->paramCount);
      for (uint32_t i = 0; i < ti->paramCount; ++i)
        params.push_back(ti->params[i].defaultValue);
    }

    PGNodeID id = pg.addFilter((uint32_t)ti->id, label, params);
    if (PGNode *node = pg.findNode(id)) {
      node->enabled = n.enabled;
      node->name = label;
      node->lutPath = n.lutPath;
    }
  }

  engine.markPostGraphDirty();
  engine.syncFilterGraphFromPostGraph();
  engine.updatePostFilters();
}

void EditorLayer::storePostGraphPersist(EngineContext &engine) {
  m_persist.postGraphFilters.clear();

  std::vector<PGNodeID> order;
  const PGCompileError err = engine.postGraph().buildChainOrder(order);
  if (!err.ok)
    return;

  for (PGNodeID id : order) {
    PGNode *n = engine.postGraph().findNode(id);
    if (!n || n->kind != PGNodeKind::Filter)
      continue;

    EditorPersistState::PostGraphPersistNode pn{};
    pn.typeId = n->typeID;
    pn.enabled = n->enabled;
    pn.label = n->name;
    pn.params = n->params;
    pn.lutPath = n->lutPath;
    m_persist.postGraphFilters.push_back(std::move(pn));
  }
}

void EditorLayer::drawStats(EngineContext &engine, GizmoState &g) {
  ImGui::Begin("Stats");
  ImGui::Text("dt: %.3f ms", engine.dt() * 1000.0f);
  ImGui::Text("Viewport: %u x %u", m_viewport.viewport().lastRenderedSize.x,
              m_viewport.viewport().lastRenderedSize.y);
  ImGui::Text("Last Pick: 0x%08X", engine.lastPickedID());
  const char *viewModeNames[] = {
      "Lit", "Albedo", "Normals", "Roughness", "Metallic",
      "AO",  "Depth",  "ID",      "LightGrid",
  };

  ImGui::SeparatorText("Gizmos");
  ImGui::Checkbox("Enable Snap", &g.useSnap);
  ImGui::DragFloat("Translate Snap", &g.snapTranslate, 0.1f, 0.001f, 100.0f);
  ImGui::DragFloat("Rotate Snap (deg)", &g.snapRotateDeg, 1.0f, 0.1f, 180.0f);
  ImGui::DragFloat("Scale Snap", &g.snapScale, 0.1f, 0.01f, 10.0f);
  ImGui::Checkbox("Propagate To Children (World)", &g.propagateChildren);
  
  ImGui::SeparatorText("View");
  int vmIdx = static_cast<int>(engine.viewMode());
  ImGui::Combo("View Mode", &vmIdx, viewModeNames, IM_ARRAYSIZE(viewModeNames));
  engine.setViewMode(static_cast<ViewMode>(vmIdx));

  const char *transNames[] = {"Sorted", "OIT"};
  int tmIdx = static_cast<int>(engine.transparencyMode());
  ImGui::Combo("Transparency", &tmIdx, transNames, IM_ARRAYSIZE(transNames));
  engine.setTransparencyMode(static_cast<TransparencyMode>(tmIdx));

  const char *shadowDebugNames[] = {
      "Off",          "Cascade Index", "Shadow Factor", "Shadow Map 0",
      "Shadow Map 1", "Shadow Map 2",  "Shadow Map 3",  "Combined",
  };
  int sdIdx = static_cast<int>(engine.shadowDebugMode());
  ImGui::Combo("Shadow Debug", &sdIdx, shadowDebugNames,
               IM_ARRAYSIZE(shadowDebugNames));
  engine.setShadowDebugMode(static_cast<ShadowDebugMode>(sdIdx));

  float alpha = engine.shadowDebugAlpha();
  if (ImGui::SliderFloat("Shadow Debug Alpha", &alpha, 0.0f, 1.0f, "%.2f")) {
    engine.setShadowDebugAlpha(alpha);
  }

  ImGui::SeparatorText("Shadow Bias");
  auto &csmCfg = engine.shadowCSMConfig();
  ImGui::Checkbox("Cull Front Faces", &csmCfg.cullFrontFaces);
  ImGui::DragFloat("Raster Slope Scale", &csmCfg.rasterSlopeScale, 0.05f, 0.0f,
                   10.0f, "%.2f");
  ImGui::DragFloat("Raster Constant", &csmCfg.rasterConstant, 0.05f, 0.0f,
                   10.0f, "%.2f");
  ImGui::DragFloat("Normal Bias", &csmCfg.normalBias, 0.0001f, 0.0f, 0.05f,
                   "%.4f");
  ImGui::DragFloat("Receiver Bias", &csmCfg.receiverBias, 0.0001f, 0.0f, 0.01f,
                   "%.4f");
  ImGui::DragFloat("Slope Bias", &csmCfg.slopeBias, 0.0001f, 0.0f, 0.02f,
                   "%.4f");
  ImGui::End();
}

void EditorLayer::processWorldEvents(EngineContext &engine) {
  if (!m_world)
    return;
  m_history.setWorld(m_world, &engine.materials());
  m_history.setAbsorbMaterialOnlyChanges(m_absorbMaterialHistoryAfterSceneLoad);
  if (m_ignoreDirtyFramesAfterSceneLoad > 0) {
    // Scene load/open can trigger non-authoring material churn. Keep history
    // baseline synced but do not record entries during this warm-up window.
    m_history.clear();
    m_world->events().clear();
    m_hierarchy.setWorld(m_world);
    return;
  }
  if (m_history.isApplying()) {
    m_hierarchy.setWorld(m_world);
    m_world->events().clear();
    return;
  }
  const auto &events = m_world->events().events();
  m_history.processEvents(*m_world, m_world->events(), engine.materials(), m_sel);
  for (const auto &e : events) {
    m_hierarchy.onWorldEvent(*m_world, e);
    if (e.type == WorldEventType::EntityDestroyed) {
      m_sel.removePicksForEntity(e.a);
      m_sel.cycleIndexByEntity.erase(e.a);
      if (m_sel.activeEntity == e.a)
        m_sel.activeEntity = InvalidEntity;
    }
  }
}

void EditorLayer::syncWorldEvents(EngineContext &engine) {
  processWorldEvents(engine);
}

bool EditorLayer::requestSaveScene(EngineContext &engine) {
  if (!m_world || !m_sceneManager)
    return false;
  if (m_sceneManager->hasActive()) {
    if (!m_sceneManager->saveActive()) {
      Log::Warn("Failed to save scene to {}", m_scenePath);
      return false;
    }
    markSceneClean(engine);
    return true;
  }
  m_saveScenePopup = true;
  std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s",
                m_scenePath.c_str());
  return false;
}

void EditorLayer::requestSaveSceneAs() {
  m_saveScenePopup = true;
  std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s",
                m_scenePath.c_str());
}

void EditorLayer::markSceneClean(EngineContext &engine) {
  m_lastAutoSaveSerial = engine.materials().changeSerial();
  m_lastCleanHistoryRevision = m_history.revision();
  m_lastObservedHistoryRevision = m_lastCleanHistoryRevision;
  if (m_world)
    m_world->clearEvents();
  if (m_sceneManager && m_sceneManager->hasActive())
    m_sceneManager->active().dirty = false;
}

bool EditorLayer::undo(EngineContext &engine) {
  if (!m_world)
    return false;
  const bool ok = m_history.undo(*m_world, engine.materials(), m_sel);
  if (ok) {
    if (m_sceneManager && m_sceneManager->hasActive())
      m_sceneManager->active().dirty = true;
    engine.rebuildEntityIndexMap();
    engine.rebuildRenderables();
  }
  return ok;
}

bool EditorLayer::redo(EngineContext &engine) {
  if (!m_world)
    return false;
  const bool ok = m_history.redo(*m_world, engine.materials(), m_sel);
  if (ok) {
    if (m_sceneManager && m_sceneManager->hasActive())
      m_sceneManager->active().dirty = true;
    engine.rebuildEntityIndexMap();
    engine.rebuildRenderables();
  }
  return ok;
}

void EditorLayer::beginGizmoHistoryBatch() {
  if (!m_world)
    return;
  m_history.beginTransformBatch("Gizmo Transform", *m_world, m_sel);
}

void EditorLayer::endGizmoHistoryBatch() {
  if (!m_world)
    return;
  m_history.endTransformBatch(*m_world, m_sel);
}

void EditorLayer::syncAssetRegistry() {
  if (!m_projectManager || !m_projectManager->hasProject()) {
    if (!m_assetProjectFileAbs.empty()) {
      m_assetProjectFileAbs.clear();
      m_assets.shutdown();
      m_assetBrowser.setRegistry(nullptr);
      m_assetBrowser.setRoot(std::filesystem::current_path() / "assets");
      m_assetBrowser.refresh();
    }
    return;
  }

  const std::string &projectFileAbs = m_projectManager->runtime().projectFileAbs();
  if (projectFileAbs != m_assetProjectFileAbs) {
    m_assetProjectFileAbs = projectFileAbs;
    m_assets.init(m_projectManager->runtime());
    m_assetBrowser.setRegistry(&m_assets);
    m_assetBrowser.setCurrentFolder(m_assets.contentRootRel());
    m_assetBrowser.refresh();
  }
}

void EditorLayer::onImGui(EngineContext &engine) {
  engine.resetUiFrameFlags();
  if (m_world)
    m_history.setWorld(m_world, &engine.materials());
  m_history.setAnimationContext(&engine.animation(), &engine.activeClip());
  m_assetBrowser.init(engine.materials().textures());
  syncAssetRegistry();
  if (!m_postGraphLoaded) {
    applyPostGraphPersist(engine);
    if (m_persist.postGraphFilters.empty()) {
      storePostGraphPersist(engine);
    }
    m_postGraphLoaded = true;
  }
  updateSceneSerialAndHistoryState(engine);
  updateSceneDirtyState(engine);
  drawMainMenuBar(engine);
  drawSceneFilePopups(engine);
  drawProjectAndSceneBrowsers(engine);
  if (drawNoWorldFallback())
    return;
  configureSequencerBindings(engine);
  drawEditorPanels(engine);
}

} // namespace Nyx
