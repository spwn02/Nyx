#include "EditorLayer.h"

#include "app/EngineContext.h"
#include "core/Log.h"
#include "scene/EntityID.h"
#include "scene/Pick.h"
#include "scene/WorldSerializer.h"
#include "tools/EditorDockLayout.h"
#include "tools/EditorPersist.h"
#include <filesystem>
#include <glm/glm.hpp>

namespace Nyx {

static std::string editorStatePath() {
  return std::filesystem::current_path() / ".nyx" / "editor_state.ini";
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
}

void EditorLayer::onDetach() {
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

static void drawProjectSettings(GizmoState &g) {
  ImGui::Begin("Project Settings");

  ImGui::SeparatorText("Gizmos");
  ImGui::Checkbox("Enable Snap", &g.useSnap);
  ImGui::DragFloat("Translate Snap", &g.snapTranslate, 0.1f, 0.001f, 100.0f);
  ImGui::DragFloat("Rotate Snap (deg)", &g.snapRotateDeg, 1.0f, 0.1f, 180.0f);
  ImGui::DragFloat("Scale Snap", &g.snapScale, 0.1f, 0.01f, 10.0f);

  ImGui::End();
}

void EditorLayer::drawStats(EngineContext &engine) {
  ImGui::Begin("Stats");
  ImGui::Text("dt: %.3f ms", engine.dt() * 1000.0f);
  ImGui::Text("Viewport: %u x %u", m_viewport.viewport().lastRenderedSize.x,
              m_viewport.viewport().lastRenderedSize.y);
  ImGui::Text("Last Pick: 0x%08X", engine.lastPickedID());
  const char *viewModeNames[] = {
      "Lit", "Albedo", "Normals", "Roughness", "Metallic", "AO", "Depth", "ID",
      "LightGrid",
  };
  int vmIdx = static_cast<int>(engine.viewMode());
  ImGui::Combo("View Mode", &vmIdx, viewModeNames, IM_ARRAYSIZE(viewModeNames));
  engine.setViewMode(static_cast<ViewMode>(vmIdx));

  const char *shadowDebugNames[] = {
      "Off",
      "Cascade Index",
      "Shadow Factor",
      "Shadow Map 0",
      "Shadow Map 1",
      "Shadow Map 2",
      "Shadow Map 3",
      "Combined",
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

void EditorLayer::processWorldEvents() {
  if (!m_world)
    return;
  const auto &events = m_world->events().events();
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

void EditorLayer::syncWorldEvents() { processWorldEvents(); }

void EditorLayer::onImGui(EngineContext &engine) {
  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New Scene")) {
        defaultScene(engine);
      }
      if (ImGui::MenuItem("Open Scene...")) {
        m_openScenePopup = true;
        std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s",
                      m_scenePath.c_str());
      }
      if (ImGui::MenuItem("Save Scene")) {
        if (!m_scenePath.empty() && m_world) {
          if (!WorldSerializer::saveToFile(*m_world, m_editorCamera,
                                           engine.materials(), m_scenePath)) {
            Log::Warn("Failed to save scene to {}", m_scenePath);
          }
        } else {
          m_saveScenePopup = true;
          std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s",
                        m_scenePath.c_str());
        }
      }
      if (ImGui::MenuItem("Save Scene As...")) {
        m_saveScenePopup = true;
        std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s",
                      m_scenePath.c_str());
      }
      ImGui::Separator();
      ImGui::MenuItem("Auto Save", nullptr, &m_autoSave);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
      if (ImGui::MenuItem("Reset Layout")) {
        m_persist.dockLayoutApplied = false; // allow rebuild
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

  // Viewport panel
  if (m_persist.panels.viewport)
    m_viewport.draw(engine, *this);

  // Stats panel
  if (m_persist.panels.stats)
    drawStats(engine);

  // Project settings panel
  if (m_persist.panels.projectSettings)
    drawProjectSettings(m_viewport.gizmoState());

  // Hierarchy panel
  if (m_persist.panels.hierarchy)
    m_hierarchy.draw(*m_world, m_editorCamera, m_sel);

  // Add menu (Shift+A)
  const bool allowOpen = !ImGui::GetIO().WantTextInput;
  m_add.tick(*m_world, m_sel, allowOpen);

  // Inspector panel
  if (m_persist.panels.inspector)
    m_inspector.draw(*m_world, engine, m_sel);

  // Sky panel
  if (m_persist.panels.sky)
    drawSkyPanel(*m_world);

  if (m_autoSave && m_sceneLoaded && !m_scenePath.empty() &&
      !m_world->events().empty()) {
    WorldSerializer::saveToFile(*m_world, m_editorCamera, engine.materials(),
                                m_scenePath);
  }

  // Asset Browser panel
  if (m_persist.panels.assetBrowser) {
    ImGui::Begin("Asset Browser");
    ImGui::TextUnformatted("Asset Browser not yet implemented");
    ImGui::End();
  }

  const auto &gizmo = m_viewport.gizmoState();

  m_persist.gizmoOp = gizmo.op;
  m_persist.gizmoMode = gizmo.mode;
  m_persist.gizmoUseSnap = gizmo.useSnap;
  m_persist.gizmoSnapTranslate = gizmo.snapTranslate;
  m_persist.gizmoSnapRotateDeg = gizmo.snapRotateDeg;
  m_persist.gizmoSnapScale = gizmo.snapScale;
}

} // namespace Nyx
