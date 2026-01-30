#include "EditorLayer.h"

#include "app/EngineContext.h"
#include "core/Log.h"
#include "editor/CameraGizmosOverlay.h"
#include "editor/CameraFrameOverlay.h"
#include "editor/EditorDockLayout.h"
#include "editor/EditorPersist.h"
#include "editor/LightGizmosOverlay.h"
#include "editor/ViewportProjector.h"
#include "editor/LockCameraToView.h"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "scene/CameraSystem.h"
#include "scene/Pick.h"
#include "scene/WorldSerializer.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <imgui.h>
#include <cstdio>

#include <ImGuizmo.h>
#include <unordered_set>

namespace Nyx {

static glm::vec3 cameraFront(float yawDeg, float pitchDeg) {
  const float yaw = glm::radians(yawDeg);
  const float pitch = glm::radians(pitchDeg);
  glm::vec3 f{cos(yaw) * cos(pitch), sin(pitch), sin(yaw) * cos(pitch)};
  return glm::normalize(f);
}

static glm::quat cameraRotation(const glm::vec3 &front) {
  const glm::vec3 up(0.0f, 1.0f, 0.0f);
  const glm::vec3 f = glm::normalize(front);
  const glm::vec3 r = glm::normalize(glm::cross(f, up));
  const glm::vec3 u = glm::cross(r, f);
  glm::mat3 m(1.0f);
  m[0] = r;
  m[1] = u;
  m[2] = -f;
  return glm::quat_cast(m);
}

static void applyEditorCameraController(World &world, EntityID camEnt,
                                        const EditorCameraController &ctrl) {
  if (camEnt == InvalidEntity || !world.isAlive(camEnt) ||
      !world.hasCamera(camEnt))
    return;

  auto &tr = world.transform(camEnt);
  tr.translation = ctrl.position;
  tr.rotation = cameraRotation(cameraFront(ctrl.yawDeg, ctrl.pitchDeg));
  tr.dirty = true;

  auto &cam = world.ensureCamera(camEnt);
  cam.fovYDeg = ctrl.fovYDeg;
  cam.nearZ = ctrl.nearZ;
  cam.farZ = ctrl.farZ;
  cam.dirty = true;
}

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

  m_gizmo.op = m_persist.gizmoOp;
  m_gizmo.mode = m_persist.gizmoMode;
  m_gizmo.useSnap = m_persist.gizmoUseSnap;
  m_gizmo.snapTranslate = m_persist.gizmoSnapTranslate;
  m_gizmo.snapRotateDeg = m_persist.gizmoSnapRotateDeg;
  m_gizmo.snapScale = m_persist.gizmoSnapScale;
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

  m_persist.gizmoOp = m_gizmo.op;
  m_persist.gizmoMode = m_gizmo.mode;
  m_persist.gizmoUseSnap = m_gizmo.useSnap;
  m_persist.gizmoSnapTranslate = m_gizmo.snapTranslate;
  m_persist.gizmoSnapRotateDeg = m_gizmo.snapRotateDeg;
  m_persist.gizmoSnapScale = m_gizmo.snapScale;
  auto res = EditorPersist::save(editorStatePath(), m_persist);
  if (!res) {
    Log::Warn("EditorPersist save failed: {}", res.error());
  }
}

void EditorLayer::setWorld(World *world) {
  m_world = world;
  m_hierarchy.setWorld(world);
}

static const char *meshTypeName(ProcMeshType t) {
  switch (t) {
  case ProcMeshType::Cube:
    return "Cube";
  case ProcMeshType::Plane:
    return "Plane";
  case ProcMeshType::Circle:
    return "Circle";
  case ProcMeshType::Sphere:
    return "Sphere";
  case ProcMeshType::Monkey:
    return "Monkey";
  default:
    return "Unknown";
  }
}

static bool meshTypeCombo(const char *label, ProcMeshType &t) {
  const char *cur = meshTypeName(t);
  bool changed = false;
  if (ImGui::BeginCombo(label, cur)) {
    auto item = [&](ProcMeshType v) {
      bool isSel = (t == v);
      if (ImGui::Selectable(meshTypeName(v), isSel)) {
        t = v;
        changed = true;
      }
      if (isSel)
        ImGui::SetItemDefaultFocus();
    };
    item(ProcMeshType::Cube);
    item(ProcMeshType::Plane);
    item(ProcMeshType::Circle);
    item(ProcMeshType::Sphere);
    item(ProcMeshType::Monkey);
    ImGui::EndCombo();
  }
  return changed;
}

// static glm::vec3 cameraFront(float yawDeg, float pitchDeg) {
//   const float yaw = glm::radians(yawDeg);
//   const float pitch = glm::radians(pitchDeg);
//   glm::vec3 f{cos(yaw) * cos(pitch), sin(pitch), sin(yaw) * cos(pitch)};
//   return glm::normalize(f);
// }

static void drawGizmoToolbar(GizmoState &g) {
  ImGui::Begin("Gizmo");

  if (ImGui::RadioButton("T", g.op == GizmoOp::Translate))
    g.op = GizmoOp::Translate;
  ImGui::SameLine();
  if (ImGui::RadioButton("R", g.op == GizmoOp::Rotate))
    g.op = GizmoOp::Rotate;
  ImGui::SameLine();
  if (ImGui::RadioButton("S", g.op == GizmoOp::Scale))
    g.op = GizmoOp::Scale;

  ImGui::SameLine();
  ImGui::SeparatorText("");

  if (ImGui::RadioButton("Local", g.mode == GizmoMode::Local))
    g.mode = GizmoMode::Local;
  ImGui::SameLine();
  if (ImGui::RadioButton("World", g.mode == GizmoMode::World))
    g.mode = GizmoMode::World;

  ImGui::Checkbox("Snap", &g.useSnap);
  if (g.useSnap) {
    ImGui::DragFloat("Snap T", &g.snapTranslate, 0.1f, 0.001f, 100.0f);
    ImGui::DragFloat("Snap R", &g.snapRotateDeg, 1.0f, 0.1f, 180.0f);
    ImGui::DragFloat("Snap S", &g.snapScale, 0.1f, 0.01f, 10.0f);
  }

  ImGui::End();
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

static ImGuizmo::OPERATION toImGuizmoOp(GizmoOp op) {
  switch (op) {
  case GizmoOp::Translate:
    return ImGuizmo::TRANSLATE;
  case GizmoOp::Rotate:
    return ImGuizmo::ROTATE;
  case GizmoOp::Scale:
    return ImGuizmo::SCALE;
  }
  return ImGuizmo::TRANSLATE;
}

static ImGuizmo::MODE toImGuizmoMode(GizmoMode m) {
  return m == GizmoMode::World ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
}

void EditorLayer::drawViewport(EngineContext &engine) {
  ImGui::Begin("Viewport");

  CameraSystem cameras;
  EntityID viewCam = InvalidEntity;

  if (m_world) {
    const bool prevLock = m_lockCam.enabled;
    ImGui::Checkbox("Lock Camera to View", &m_lockCam.enabled);
    if (!prevLock && m_lockCam.enabled) {
      EditorCameraState st{};
      st.position = m_cameraCtrl.position;
      st.yawDeg = m_cameraCtrl.yawDeg;
      st.pitchDeg = m_cameraCtrl.pitchDeg;
      m_lockCam.onToggled(*m_world, m_world->activeCamera(), st);
      m_cameraCtrl.position = st.position;
      m_cameraCtrl.yawDeg = st.yawDeg;
      m_cameraCtrl.pitchDeg = st.pitchDeg;
    }
    ImGui::Separator();

    ImGui::Checkbox("View Through Camera", &m_viewThroughCamera);
    ImGui::Separator();
  }

  if (m_viewportTex != 0) {
    ImGuiIO &io = ImGui::GetIO();

    // Available size in *ImGui logical units*
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1)
      avail.x = 1;
    if (avail.y < 1)
      avail.y = 1;

    // Convert to framebuffer pixels (DPI perfect)
    uint32_t pxW =
        (uint32_t)std::max(1.0f, avail.x * io.DisplayFramebufferScale.x);
    uint32_t pxH =
        (uint32_t)std::max(1.0f, avail.y * io.DisplayFramebufferScale.y);

    m_viewport.desiredSize = {pxW, pxH};

    // Draw the viewport image (use avail size in logical units)
    ImTextureID tid = (ImTextureID)(uintptr_t)m_viewportTex;
    ImGui::Image(tid, avail, ImVec2(0, 1), ImVec2(1, 0)); // flip Y for ImGui

    m_viewport.imageMin = {ImGui::GetItemRectMin().x,
                           ImGui::GetItemRectMin().y};
    m_viewport.imageMax = {ImGui::GetItemRectMax().x,
                           ImGui::GetItemRectMax().y};

    m_viewport.hovered = ImGui::IsItemHovered();
    m_viewport.focused =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  }

  if (m_world) {
    if (m_viewport.desiredSize != m_viewport.lastRenderedSize) {
      const EntityID camEnt =
          m_viewThroughCamera ? m_world->activeCamera() : m_cameraEntity;
      if (camEnt != InvalidEntity && m_world->hasCamera(camEnt)) {
        m_world->camera(camEnt).dirty = true;
      }
    }
    cameras.update(*m_world, m_viewport.desiredSize.x,
                   m_viewport.desiredSize.y);

    viewCam =
        m_viewThroughCamera ? m_world->activeCamera() : m_cameraEntity;
    if (viewCam == InvalidEntity || !m_world->hasCamera(viewCam))
      viewCam = m_world->activeCamera();
  }

  if (m_world && m_viewport.hasImageRect()) {
    if (viewCam != InvalidEntity && m_world->hasCamera(viewCam)) {
      const uint32_t w = std::max(1u, m_viewport.desiredSize.x);
      const uint32_t h = std::max(1u, m_viewport.desiredSize.y);
      cameras.update(*m_world, w, h);
      const auto &mats = m_world->cameraMatrices(viewCam);

      static CameraFrameOverlay frameOverlay;
      frameOverlay.draw({m_viewport.imageMin.x, m_viewport.imageMin.y},
                        {m_viewport.imageMax.x, m_viewport.imageMax.y},
                        m_viewThroughCamera);

      static CameraGizmosOverlay overlay;
      CameraOverlaySettings settings{};
      settings.showAllCameras = true;
      settings.hideActiveCamera = true;
      settings.hideEntity = InvalidEntity;
      settings.frustumDepth = 2.5f;

      auto isSelected = [&](EntityID e) -> bool {
        return m_sel.hasPick(packPick(e, 0));
      };

      overlay.draw(*m_world, mats.viewProj,
                   {m_viewport.imageMin.x, m_viewport.imageMin.y},
                   {m_viewport.imageMax.x, m_viewport.imageMax.y}, isSelected,
                   settings);

      ViewportProjector proj{};
      proj.viewProj = mats.viewProj;
      proj.imageMin = ImVec2(m_viewport.imageMin.x, m_viewport.imageMin.y);
      proj.imageMax = ImVec2(m_viewport.imageMax.x, m_viewport.imageMax.y);
      proj.fbWidth = m_viewport.lastRenderedSize.x;
      proj.fbHeight = m_viewport.lastRenderedSize.y;

      static LightGizmosOverlay lightOverlay;
      lightOverlay.draw(*m_world, m_sel, proj);
    }
  }

  // --- ImGuizmo ---
  ImGuizmo::BeginFrame();

  Selection &sel = m_sel;
  const uint32_t activePick =
      sel.activePick ? sel.activePick
                     : (sel.picks.empty() ? 0u : sel.picks.front());
  m_gizmoUsing = false;
  m_gizmoOver = false;

  if (sel.kind == SelectionKind::Picks && activePick != 0u) {
    EntityID e = sel.activeEntity;
    if (e == InvalidEntity) {
      const uint32_t slotIndex = pickEntity(activePick);
      e = engine.resolveEntityIndex(slotIndex);
    }
    if (e != InvalidEntity && engine.world().isAlive(e)) {
      auto &world = engine.world();

      const EntityID camEnt =
          (viewCam != InvalidEntity) ? viewCam : world.activeCamera();
      if (camEnt == InvalidEntity || !world.hasCamera(camEnt)) {
        ImGui::End();
        return;
      }

      const uint32_t w = std::max(1u, m_viewport.desiredSize.x);
      const uint32_t h = std::max(1u, m_viewport.desiredSize.y);
      cameras.update(world, w, h);
      const auto &mats = world.cameraMatrices(camEnt);

      // ImGuizmo setup
      ImGuizmo::SetOrthographic(false);
      ImGuizmo::SetDrawlist();

      ImGuizmo::SetRect(m_viewport.imageMin.x, m_viewport.imageMin.y,
                        (m_viewport.imageMax.x - m_viewport.imageMin.x),
                        (m_viewport.imageMax.y - m_viewport.imageMin.y));

      // We manipulate WORLD matrix for best experience.
      // CWorldTransform.world)
      world.updateTransforms(); // ensure up-to-date before gizmo

      glm::mat4 worldM = world.worldTransform(e).world;
      const glm::mat4 prevWorldM = worldM;

      // Snap values
      float snap[3] = {0, 0, 0};
      const bool doSnap = m_gizmo.useSnap;
      if (doSnap) {
        if (m_gizmo.op == GizmoOp::Translate) {
          snap[0] = snap[1] = snap[2] = m_gizmo.snapTranslate;
        } else if (m_gizmo.op == GizmoOp::Rotate) {
          snap[0] = snap[1] = snap[2] = m_gizmo.snapRotateDeg;
        } else if (m_gizmo.op == GizmoOp::Scale) {
          snap[0] = snap[1] = snap[2] = m_gizmo.snapScale;
        }
      }

      // Manipulate
      ImGuizmo::OPERATION op = toImGuizmoOp(m_gizmo.op);
      ImGuizmo::MODE mode = toImGuizmoMode(m_gizmo.mode);
      if (world.hasLight(e)) {
        const auto &L = world.light(e);
        if (L.type == LightType::Point && op == ImGuizmo::ROTATE)
          op = ImGuizmo::TRANSLATE;
        if (op == ImGuizmo::SCALE)
          op = ImGuizmo::TRANSLATE;
      }

      // Allow gizmo input only when the mouse is over the viewport image.
      ImGuizmo::Enable(true);
      ImGuizmo::Manipulate(
          glm::value_ptr(mats.view), glm::value_ptr(mats.proj), op, mode,
          glm::value_ptr(worldM), nullptr, doSnap ? snap : nullptr);

      m_gizmoUsing = ImGuizmo::IsUsing();
      m_gizmoOver = ImGuizmo::IsOver();

      if (m_gizmoUsing) {
        // Convert worldM back into LOCAL transform (preserve parenting)
        // We need parent world matrix inverse
        EntityID p = world.parentOf(e);
        glm::mat4 parentW(1.0f);
        if (p != InvalidEntity) {
          parentW = world.worldTransform(p).world;
        }
        glm::mat4 localM = glm::inverse(parentW) * worldM;

        // Decompose localM into TRS and write to CTransform
        glm::vec3 t, s, skew;
        glm::vec4 persp;
        glm::quat r;
        glm::decompose(localM, s, r, t, skew, persp);
        if (s.x <= 0.01f)
          s.x = 0.01f;
        if (s.y <= 0.01f)
          s.y = 0.01f;
        if (s.z <= 0.01f)
          s.z = 0.01f;

        auto &tr = world.transform(e);
        tr.translation = t;
        tr.rotation = r;
        tr.scale = s;
        tr.dirty = true;
        world.worldTransform(e).dirty = true;
        world.events().push({WorldEventType::TransformChanged, e});

        if (sel.picks.size() > 1) {
          const glm::mat4 delta = worldM * glm::inverse(prevWorldM);
          std::unordered_set<EntityID, EntityHash> selected;
          selected.reserve(sel.picks.size());
          std::vector<EntityID> entities;
          entities.reserve(sel.picks.size());
          for (uint32_t pick : sel.picks) {
            EntityID pe = sel.entityForPick(pick);
            if (pe == InvalidEntity)
              pe = engine.resolveEntityIndex(pickEntity(pick));
            if (pe == InvalidEntity || !world.isAlive(pe))
              continue;
            if (selected.insert(pe).second)
              entities.push_back(pe);
          }

          for (EntityID pe : entities) {
            if (pe == e)
              continue;
            glm::mat4 otherWorld = world.worldTransform(pe).world;
            glm::mat4 newWorld = delta * otherWorld;

            EntityID parent = world.parentOf(pe);
            glm::mat4 parentW(1.0f);
            if (parent != InvalidEntity) {
              parentW = world.worldTransform(parent).world;
              if (selected.find(parent) != selected.end())
                parentW = delta * parentW;
            }
            glm::mat4 localOther = glm::inverse(parentW) * newWorld;

            glm::vec3 ot, os, oskew;
            glm::vec4 opersp;
            glm::quat orot;
            glm::decompose(localOther, os, orot, ot, oskew, opersp);
            if (os.x <= 0.01f)
              os.x = 0.01f;
            if (os.y <= 0.01f)
              os.y = 0.01f;
            if (os.z <= 0.01f)
              os.z = 0.01f;

            auto &otr = world.transform(pe);
            otr.translation = ot;
            otr.rotation = orot;
            otr.scale = os;
            otr.dirty = true;
            world.worldTransform(pe).dirty = true;
            world.events().push({WorldEventType::TransformChanged, pe});
          }
        }
      }
    }
  }

  ImGui::End();
}

void EditorLayer::drawStats(EngineContext &engine) {
  ImGui::Begin("Stats");
  ImGui::Text("dt: %.3f ms", engine.dt() * 1000.0f);
  ImGui::Text("Viewport: %u x %u", m_viewport.lastRenderedSize.x,
              m_viewport.lastRenderedSize.y);
  ImGui::Text("Last Pick: 0x%08X", engine.lastPickedID());
  const char *viewModeNames[] = {
      "Lit", "Albedo", "Normals", "Roughness", "Metallic", "AO", "Depth", "ID",
  };
  int vmIdx = static_cast<int>(engine.viewMode());
  ImGui::Combo("View Mode", &vmIdx, viewModeNames, IM_ARRAYSIZE(viewModeNames));
  engine.setViewMode(static_cast<ViewMode>(vmIdx));
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
      if (ImGui::MenuItem("Open Scene...")) {
        m_openScenePopup = true;
        std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s",
                      m_scenePath.c_str());
      }
      if (ImGui::MenuItem("Save Scene")) {
        if (!m_scenePath.empty() && m_world) {
          if (!WorldSerializer::saveToFile(*m_world, m_scenePath)) {
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
        if (WorldSerializer::loadFromFile(*m_world, path)) {
          m_scenePath = path;
          m_sceneLoaded = true;
          m_sel.clear();
          m_hierarchy.setWorld(m_world);
          engine.rebuildEntityIndexMap();
          engine.rebuildRenderables();

          EntityID editorCam = m_world->createEntity("Editor Camera");
          if (editorCam != InvalidEntity) {
            m_world->ensureCamera(editorCam);
            setCameraEntity(editorCam);
            applyEditorCameraController(*m_world, editorCam, m_cameraCtrl);
            if (m_world->activeCamera() == InvalidEntity)
              m_world->setActiveCamera(editorCam);
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
        if (WorldSerializer::saveToFile(*m_world, path)) {
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
    drawViewport(engine);

  // Stats panel
  if (m_persist.panels.stats)
    drawStats(engine);

  // Gizmo toolbar
  drawGizmoToolbar(m_gizmo);

  // Project settings panel
  if (m_persist.panels.projectSettings)
    drawProjectSettings(m_gizmo);

  // Hierarchy panel
  if (m_persist.panels.hierarchy)
    m_hierarchy.draw(*m_world, m_sel);

  // Add menu (Shift+A)
  const bool allowOpen = !ImGui::GetIO().WantTextInput;
  m_add.tick(*m_world, m_sel, allowOpen);

  // Inspector panel
  if (m_persist.panels.inspector)
    m_inspector.draw(*m_world, engine, m_sel);

  if (m_autoSave && m_sceneLoaded && !m_scenePath.empty() &&
      !m_world->events().empty()) {
    WorldSerializer::saveToFile(*m_world, m_scenePath);
  }

  // Asset Browser panel
  if (m_persist.panels.assetBrowser) {
    ImGui::Begin("Asset Browser");
    ImGui::TextUnformatted("Asset Browser not yet implemented");
    ImGui::End();
  }

  m_persist.gizmoOp = m_gizmo.op;
  m_persist.gizmoMode = m_gizmo.mode;
  m_persist.gizmoUseSnap = m_gizmo.useSnap;
  m_persist.gizmoSnapTranslate = m_gizmo.snapTranslate;
  m_persist.gizmoSnapRotateDeg = m_gizmo.snapRotateDeg;
  m_persist.gizmoSnapScale = m_gizmo.snapScale;
}

} // namespace Nyx
