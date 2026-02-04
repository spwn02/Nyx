#include "ViewportPanel.h"
#include "app/EngineContext.h"
#include "editor/EditorLayer.h"
#include "editor/tools/LockCameraToView.h"
#include "editor/tools/ViewportProjector.h"
#include "editor/ui/CameraFrameOverlay.h"
#include "editor/ui/CameraGizmosOverlay.h"
#include "editor/ui/LightGizmosOverlay.h"

#include "glm/gtc/type_ptr.hpp"
#include "imgui.h"

#include "ImGuizmo.h"
#include "glm/gtx/matrix_decompose.hpp"
#include "scene/CameraSystem.h"
#include "scene/EntityID.h"
#include "scene/Pick.h"
#include <unordered_set>

namespace Nyx {

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

void ViewportPanel::draw(EngineContext &engine, EditorLayer &editor) {
  ImGui::Begin("Viewport");

  CameraSystem cameras;
  EntityID viewCam = InvalidEntity;

  if (editor.world()) {
    const bool prevLock = m_lockCam.enabled;
    const bool prevView = m_viewThroughCamera;
    const EditorCameraState preCam{editor.cameraController().position,
                                   editor.cameraController().yawDeg,
                                   editor.cameraController().pitchDeg};
    ImGui::Checkbox("Lock Camera to View", &m_lockCam.enabled);
    if (!prevLock && m_lockCam.enabled) {
      EditorCameraState st{};
      st.position = editor.cameraController().position;
      st.yawDeg = editor.cameraController().yawDeg;
      st.pitchDeg = editor.cameraController().pitchDeg;
      m_lockCam.onToggled(*editor.world(), editor.world()->activeCamera(), st);
      editor.cameraController().position = st.position;
      editor.cameraController().yawDeg = st.yawDeg;
      editor.cameraController().pitchDeg = st.pitchDeg;
    }
    ImGui::Separator();

    ImGui::Checkbox("View Through Camera", &m_viewThroughCamera);
    ImGui::Separator();

    if (!prevView && m_viewThroughCamera) {
      m_savedEditorCamState = preCam;
      m_savedEditorCam = true;
    } else if (prevView && !m_viewThroughCamera) {
      if (m_savedEditorCam) {
        editor.cameraController().position = m_savedEditorCamState.position;
        editor.cameraController().yawDeg = m_savedEditorCamState.yawDeg;
        editor.cameraController().pitchDeg = m_savedEditorCamState.pitchDeg;
        if (editor.world() && editor.editorCamera() != InvalidEntity)
          editor.cameraController().apply(*editor.world(),
                                          editor.editorCamera());
      }
      m_savedEditorCam = false;
    }
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

  if (editor.world()) {
    if (m_viewport.desiredSize != m_viewport.lastRenderedSize) {
      const EntityID camEnt = m_viewThroughCamera
                                  ? editor.world()->activeCamera()
                                  : editor.cameraEntity();
      if (camEnt != InvalidEntity && editor.world()->hasCamera(camEnt)) {
        editor.world()->camera(camEnt).dirty = true;
      }
    }
    cameras.update(*editor.world(), m_viewport.desiredSize.x,
                   m_viewport.desiredSize.y);

    viewCam = m_viewThroughCamera ? editor.world()->activeCamera()
                                  : editor.cameraEntity();
    if (viewCam == InvalidEntity || !editor.world()->hasCamera(viewCam))
      viewCam = editor.world()->activeCamera();

    if (m_viewport.hasImageRect()) {
      if (viewCam != InvalidEntity && editor.world()->hasCamera(viewCam)) {
        const uint32_t w = std::max(1u, m_viewport.desiredSize.x);
        const uint32_t h = std::max(1u, m_viewport.desiredSize.y);
        cameras.update(*editor.world(), w, h);
        const auto &mats = editor.world()->cameraMatrices(viewCam);
        m_frameOverlay.draw({m_viewport.imageMin.x, m_viewport.imageMin.y},
                            {m_viewport.imageMax.x, m_viewport.imageMax.y},
                            m_viewThroughCamera);

        CameraOverlaySettings settings{};
        settings.showAllCameras = true;
        settings.hideActiveCamera = m_viewThroughCamera;
        settings.hideEntity = editor.editorCamera();
        settings.frustumDepth = 2.5f;

        auto isSelected = [&](EntityID e) -> bool {
          return editor.selection().hasPick(packPick(e, 0));
        };

        m_cameraGizmos.draw(*editor.world(), mats.viewProj,
                            {m_viewport.imageMin.x, m_viewport.imageMin.y},
                            {m_viewport.imageMax.x, m_viewport.imageMax.y},
                            isSelected, settings);

        ViewportProjector proj{};
        proj.viewProj = mats.viewProj;
        proj.imageMin = ImVec2(m_viewport.imageMin.x, m_viewport.imageMin.y);
        proj.imageMax = ImVec2(m_viewport.imageMax.x, m_viewport.imageMax.y);
        proj.fbWidth = m_viewport.lastRenderedSize.x;
        proj.fbHeight = m_viewport.lastRenderedSize.y;

        m_lightOverlay.draw(*editor.world(), editor.selection(), proj);
      }
    }
  }

  // --- ImGuizmo ---
  ImGuizmo::BeginFrame();

  Selection &sel = editor.selection();
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
      ImGuizmo::Manipulate(glm::value_ptr(mats.view), glm::value_ptr(mats.proj),
                           op, mode, glm::value_ptr(worldM), nullptr,
                           doSnap ? snap : nullptr);

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

} // namespace Nyx
