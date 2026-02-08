#include "EditorLayer.h"

#include "app/EngineContext.h"
#include "core/Log.h"
#include "scene/EntityID.h"
#include "scene/Pick.h"
#include "scene/WorldSerializer.h"
#include "tools/EditorDockLayout.h"
#include "tools/EditorPersist.h"
#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/glm.hpp>

namespace Nyx {

static std::string editorStatePath() {
  return std::filesystem::current_path() / ".nyx" / "editor_state.ini";
}

static void enableDefaultWorkspacePanels(EditorPanels &panels) {
  panels = {};
  panels.viewport = true;
  panels.hierarchy = true;
  panels.inspector = true;
  panels.sky = true;
  panels.assetBrowser = true;
  panels.stats = true;
}

static void enableMaterialWorkspacePanels(EditorPanels &panels) {
  panels = {};
  panels.materialGraph = true;
  panels.lutManager = true;
  panels.hierarchy = true;
  panels.inspector = true;
  panels.assetBrowser = true;
  panels.sky = true;
}

static void enablePostProcessingWorkspacePanels(EditorPanels &panels) {
  panels = {};
  panels.postGraph = true;
  panels.hierarchy = true;
  panels.inspector = true;
  panels.assetBrowser = true;
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
  if (!m_world)
    return false;
  if (!m_scenePath.empty()) {
    if (!WorldSerializer::saveToFile(*m_world, m_editorCamera,
                                     engine.materials(), m_scenePath)) {
      Log::Warn("Failed to save scene to {}", m_scenePath);
      return false;
    }
    m_lastAutoSaveSerial = engine.materials().changeSerial();
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

bool EditorLayer::undo(EngineContext &engine) {
  if (!m_world)
    return false;
  const bool ok = m_history.undo(*m_world, engine.materials(), m_sel);
  if (ok) {
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

void EditorLayer::onImGui(EngineContext &engine) {
  engine.resetUiFrameFlags();
  if (m_world)
    m_history.setWorld(m_world, &engine.materials());
  m_history.setAnimationContext(&engine.animation(), &engine.activeClip());
  m_assetBrowser.init(engine.materials().textures());
  if (!m_postGraphLoaded) {
    applyPostGraphPersist(engine);
    if (m_persist.postGraphFilters.empty()) {
      storePostGraphPersist(engine);
    }
    m_postGraphLoaded = true;
  }

  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
        defaultScene(engine);
      }
      if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
        m_openScenePopup = true;
        std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s",
                      m_scenePath.c_str());
      }
      if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
        if (!m_scenePath.empty() && m_world) {
          if (!WorldSerializer::saveToFile(*m_world, m_editorCamera,
                                           engine.materials(), m_scenePath)) {
            Log::Warn("Failed to save scene to {}", m_scenePath);
          } else {
            m_lastAutoSaveSerial = engine.materials().changeSerial();
          }
        } else {
          m_saveScenePopup = true;
          std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s",
                        m_scenePath.c_str());
        }
      }
      if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
        m_saveScenePopup = true;
        std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s",
                      m_scenePath.c_str());
      }
      ImGui::Separator();
      ImGui::MenuItem("Auto Save", "Ctrl+Alt+S", &m_autoSave);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
      // Workspaces
      if (ImGui::BeginMenu("Workspaces")) {
        if (ImGui::MenuItem("Default")) {
          m_persist.dockLayoutApplied = false; // allow rebuild
          enableDefaultWorkspacePanels(m_persist.panels);
          const ImGuiViewport *vp = ImGui::GetMainViewport();
          BuildDefaultDockLayout(engine.dockspaceID(), vp->WorkSize);
        }
        // if (ImGui::MenuItem("Scene Editing")) {
        //   m_persist.dockLayoutApplied = false; // allow rebuild
        //   const ImGuiViewport *vp = ImGui::GetMainViewport();
        //   BuildSceneEditingDockLayout(engine.dockspaceID(), vp->WorkSize);
        // }
        if (ImGui::MenuItem("Material Editing")) {
          m_persist.dockLayoutApplied = false; // allow rebuild
          enableMaterialWorkspacePanels(m_persist.panels);
          const ImGuiViewport *vp = ImGui::GetMainViewport();
          BuildMaterialEditingDockLayout(engine.dockspaceID(), vp->WorkSize);
        }
        if (ImGui::MenuItem("Post-Processing Editing")) {
          m_persist.dockLayoutApplied = false; // allow rebuild
          enablePostProcessingWorkspacePanels(m_persist.panels);
          const ImGuiViewport *vp = ImGui::GetMainViewport();
          BuildPostProcessingEditingDockLayout(engine.dockspaceID(),
                                               vp->WorkSize);
        }
        ImGui::EndMenu();
      }

      if (ImGui::MenuItem("Reset Layout")) {
        m_persist.dockLayoutApplied = false; // allow rebuild
        enableDefaultWorkspacePanels(m_persist.panels);
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        BuildDefaultDockLayout(engine.dockspaceID(), vp->WorkSize);
      }
      ImGui::MenuItem("Viewport", nullptr, &m_persist.panels.viewport);
      ImGui::MenuItem("Hierarchy", nullptr, &m_persist.panels.hierarchy);
      ImGui::MenuItem("Inspector", nullptr, &m_persist.panels.inspector);
      ImGui::MenuItem("Sky", nullptr, &m_persist.panels.sky);
      ImGui::MenuItem("Stats", nullptr, &m_persist.panels.stats);
      ImGui::MenuItem("Project Settings", nullptr,
                      &m_persist.panels.projectSettings);
      ImGui::MenuItem("Asset Browser", nullptr, &m_persist.panels.assetBrowser);
      ImGui::MenuItem("LUT Manager", nullptr, &m_persist.panels.lutManager);
      ImGui::MenuItem("Material Graph", nullptr, &m_persist.panels.materialGraph);
      ImGui::MenuItem("Post-Processing Graph", nullptr,
                      &m_persist.panels.postGraph);
      ImGui::MenuItem("Sequencer", nullptr, &m_persist.panels.sequencer);
      ImGui::MenuItem("History", nullptr, &m_persist.panels.history);
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }

  if (m_openScenePopup) {
    m_openScenePopup = false;
    ImGui::OpenPopup("Open Scene");
  }
  if (m_saveScenePopup) {
    m_saveScenePopup = false;
    ImGui::OpenPopup("Save Scene As");
  }

  if (ImGui::BeginPopupModal("Open Scene", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!m_world) {
      ImGui::TextUnformatted("No world loaded.");
    } else {
      ImGui::InputText("Path", m_scenePathBuf, sizeof(m_scenePathBuf));
      if (ImGui::Button("Open")) {
        const std::string path(m_scenePathBuf);
        if (!path.empty()) {
          engine.resetMaterials();
          if (WorldSerializer::loadFromFile(*m_world, engine.materials(),
                                            path)) {
            m_scenePath = path;
            m_sceneLoaded = true;
            m_lastAutoSaveSerial = engine.materials().changeSerial();
            m_sel.clear();
            m_hierarchy.setWorld(m_world);
            engine.rebuildEntityIndexMap();
            engine.rebuildRenderables();
            const auto &sky = m_world->skySettings();
            if (!sky.hdriPath.empty()) {
              engine.envIBL().loadFromHDR(sky.hdriPath);
            }
          }
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Save Scene As", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!m_world) {
      ImGui::TextUnformatted("No world loaded.");
    } else {
      ImGui::InputText("Path", m_scenePathBuf, sizeof(m_scenePathBuf));
      if (ImGui::Button("Save")) {
        const std::string path(m_scenePathBuf);
        if (!path.empty()) {
          if (WorldSerializer::saveToFile(*m_world, m_editorCamera,
                                          engine.materials(), path)) {
            m_scenePath = path;
            m_sceneLoaded = true;
            m_lastAutoSaveSerial = engine.materials().changeSerial();
          } else {
            Log::Warn("Failed to save scene to {}", path);
          }
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  if (!m_world) {
    ImGui::Begin("Hierarchy");
    ImGui::TextUnformatted("No world loaded");
    ImGui::End();
    return;
  }

  // Keep sequencer bindings valid for inspector/gizmo auto-key even when
  // the Sequencer panel itself is hidden.
  m_sequencerPanel.setWorld(m_world);
  m_sequencerPanel.setAnimationSystem(&engine.animation());
  m_sequencerPanel.setAnimationClip(&engine.activeClip());
  if (m_world) {
    std::vector<EntityID> exclude;
    exclude.push_back(m_editorCamera);
    exclude.push_back(m_world->activeCamera());
    m_sequencerPanel.setHiddenExclusions(exclude);
    m_sequencerPanel.setTrackExclusions(exclude);
  }

  // Viewport panel
  if (m_persist.panels.viewport)
    m_viewport.draw(engine, *this);

  // Stats panel
  if (m_persist.panels.stats)
    drawStats(engine, m_viewport.gizmoState());

  // Project Settings panel
  if (m_persist.panels.projectSettings)
    m_projectSettings.draw(*this, engine);

  // Hierarchy panel
  if (m_persist.panels.hierarchy)
    m_hierarchy.draw(*m_world, m_editorCamera, engine, m_sel);

  // History panel
  if (m_persist.panels.history && m_world)
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

  // Add menu (Shift+A)
  const bool allowOpen =
      !ImGui::GetIO().WantTextInput && !engine.uiBlockGlobalShortcuts() &&
      !m_postGraphPanel.isHovered() && !m_materialGraphPanel.isHovered() &&
      !m_cameraCtrl.mouseCaptured;
  m_add.tick(*m_world, m_sel, allowOpen);

  // Inspector panel
  if (m_persist.panels.inspector)
    m_inspector.draw(*m_world, engine, m_sel, &m_sequencerPanel);

  // Material Graph panel (always present, auto-switch)  
  if (m_persist.panels.materialGraph) {
    MaterialHandle activeMat = InvalidMaterial;
    if (m_sel.kind == SelectionKind::Material &&
        m_sel.activeMaterial != InvalidMaterial) {
      activeMat = m_sel.activeMaterial;
    } else if (!m_sel.isEmpty()) {
      const uint32_t activePick =
          m_sel.activePick ? m_sel.activePick : m_sel.picks.back();
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

  // Sky panel
  if (m_persist.panels.sky)
    drawSkyPanel(*m_world, engine);

  if (m_autoSave && m_sceneLoaded && !m_scenePath.empty() &&
      (!m_world->events().empty() ||
       engine.materials().changeSerial() != m_lastAutoSaveSerial)) {
    if (WorldSerializer::saveToFile(*m_world, m_editorCamera,
                                    engine.materials(), m_scenePath)) {
      m_lastAutoSaveSerial = engine.materials().changeSerial();
    }
  }

  // Asset Browser panel
  if (m_persist.panels.assetBrowser) {
    m_assetBrowser.draw(&m_persist.panels.assetBrowser);
  }
  if (m_persist.panels.lutManager) {
    m_lutManager.draw(engine);
  }

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

  // Sequencer panel
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
