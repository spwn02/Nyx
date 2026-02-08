#include "InspectorPanel.h"

#include "InspectorLight.h"
#include "app/EngineContext.h"
#include "editor/ui/panels/InspectorMaterial.h"
#include "material/MaterialHandle.h"
#include "editor/ui/panels/SequencerPanel.h"
#include "scene/Pick.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <imgui.h>
#include <algorithm>
#include <string>
#include <unordered_map>

namespace Nyx {

static bool vec3Drag(const char *label, glm::vec3 &v, float speed = 0.05f) {
  return ImGui::DragFloat3(label, &v.x, speed);
}

static std::unordered_map<EntityID, glm::vec3, EntityHash> s_unwrappedEuler;

static glm::vec3 &unwrappedEulerFor(EntityID e, const glm::quat &q) {
  auto it = s_unwrappedEuler.find(e);
  if (it == s_unwrappedEuler.end()) {
    it = s_unwrappedEuler
             .emplace(e, glm::degrees(glm::eulerAngles(glm::normalize(q))))
             .first;
  } else {
    // If rotation changed externally, resync inspector cache.
    const glm::quat cachedQ = glm::normalize(glm::quat(glm::radians(it->second)));
    const glm::quat curQ = glm::normalize(q);
    const float dotAbs = std::abs(glm::dot(cachedQ, curQ));
    if (dotAbs < 0.9999f) {
      it->second = glm::degrees(glm::eulerAngles(curQ));
    }
  }
  return it->second;
}

static bool quatEditEulerDeg(const char *label, EntityID e, glm::quat &q,
                             glm::vec3 *outEulerDeg) {
  glm::vec3 &eulerDeg = unwrappedEulerFor(e, q);
  if (outEulerDeg)
    *outEulerDeg = eulerDeg;

  bool changed = ImGui::DragFloat3(label, &eulerDeg.x, 0.25f);
  if (changed) {
    const glm::vec3 r = glm::radians(eulerDeg);
    q = glm::normalize(glm::quat(r));
  }
  if (outEulerDeg)
    *outEulerDeg = eulerDeg;
  return changed;
}

static bool drawTransform(World &world, EntityID e,
                          SequencerPanel *sequencer) {
  auto &tr = world.transform(e);

  bool visibilityChanged = false;
  bool hidden = tr.hidden;
  if (ImGui::Checkbox("Hidden", &hidden)) {
    tr.hidden = hidden;
    visibilityChanged = true;
  }
  ImGui::BeginDisabled();
  bool disabledAnim = tr.disabledAnim;
  ImGui::Checkbox("Disabled (Anim)", &disabledAnim);
  ImGui::EndDisabled();

  bool changed = false;
  bool endT = false;
  bool endR = false;
  bool endS = false;
  glm::vec3 rotEulerDeg{};
  changed |= vec3Drag("Translation", tr.translation, 0.02f);
  if (ImGui::IsItemDeactivatedAfterEdit())
    endT = true;
  changed |= quatEditEulerDeg("Rotation (deg)", e, tr.rotation, &rotEulerDeg);
  if (ImGui::IsItemDeactivatedAfterEdit())
    endR = true;
  changed |= vec3Drag("Scale", tr.scale, 0.02f);
  if (ImGui::IsItemDeactivatedAfterEdit())
    endS = true;

  if (changed) {
    tr.dirty = true;
    world.worldTransform(e).dirty = true;
    world.events().push({WorldEventType::TransformChanged, e});
  }

  if (sequencer && (endT || endR || endS)) {
    uint32_t mask = 0;
    if (endT)
      mask |= SequencerPanel::EditTranslate;
    if (endR)
      mask |= SequencerPanel::EditRotate;
    if (endS)
      mask |= SequencerPanel::EditScale;
    const float *rotPtr = endR ? &rotEulerDeg.x : nullptr;
    sequencer->onTransformEditEnd(e, mask, rotPtr);
  }

  if (ImGui::Button("Reset T")) {
    tr.translation = {0.0f, 0.0f, 0.0f};
    tr.dirty = true;
    world.worldTransform(e).dirty = true;
    world.events().push({WorldEventType::TransformChanged, e});
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset R")) {
    tr.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    tr.dirty = true;
    world.worldTransform(e).dirty = true;
    world.events().push({WorldEventType::TransformChanged, e});
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset S")) {
    tr.scale = {1.0f, 1.0f, 1.0f};
    tr.dirty = true;
    world.worldTransform(e).dirty = true;
    world.events().push({WorldEventType::TransformChanged, e});
  }

  return visibilityChanged;
}

static void drawMesh(World &world, Selection &sel, EntityID e,
                     uint32_t pickedSubmesh) {
  auto &mc = world.mesh(e);
  const uint32_t n = (uint32_t)mc.submeshes.size();

  ImGui::Text("Submeshes: %u", n);

  for (uint32_t i = 0; i < n; ++i) {
    auto &sm = mc.submeshes[i];
    const bool isActive = (i == pickedSubmesh);
    if (isActive)
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.75f, 0.2f, 1));

    ImGui::BulletText("[%u] %s", i, sm.name.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(
            (std::string("Select##sm") + std::to_string(i)).c_str())) {
      sel.setSinglePick(packPick(e, i), e);
      sel.activeEntity = e;
    }

    if (isActive)
      ImGui::PopStyleColor();
  }
}

void InspectorPanel::draw(World &world, EngineContext &engine, Selection &sel,
                          SequencerPanel *sequencer) {
  ImGui::Begin("Inspector");

  if (sel.kind == SelectionKind::Material &&
      sel.activeMaterial != InvalidMaterial) {
    static InspectorMaterial matInspector;
    matInspector.draw(engine, sel.activeMaterial);
    engine.setPreviewMaterial(sel.activeMaterial);

    if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
      glm::vec3 dir = engine.previewLightDir();
      if (ImGui::DragFloat3("Light Dir", &dir.x, 0.01f, -1.0f, 1.0f)) {
        if (glm::length(dir) < 1e-4f)
          dir = glm::vec3(0.0f, 1.0f, 0.0f);
        engine.previewLightDir() = glm::normalize(dir);
      }
      float intensity = engine.previewLightIntensity();
      if (ImGui::DragFloat("Light Intensity", &intensity, 0.05f, 0.0f, 100.0f)) {
        engine.previewLightIntensity() = std::max(0.0f, intensity);
      }
      float exposure = engine.previewLightExposure();
      if (ImGui::DragFloat("Light Exposure", &exposure, 0.05f, -10.0f, 10.0f)) {
        engine.previewLightExposure() = exposure;
      }

      const uint32_t tex = engine.renderer().previewTexture();
      if (tex != 0) {
        ImGui::Image((ImTextureID)(uintptr_t)tex, ImVec2(256, 256),
                     ImVec2(0, 1), ImVec2(1, 0));
      } else {
        ImGui::TextDisabled("Preview not available.");
      }
    }

    ImGui::End();
    return;
  }

  if (sel.isEmpty()) {
    ImGui::TextUnformatted("No selection.");
    engine.setPreviewMaterial(InvalidMaterial);
    ImGui::End();
    return;
  }

  if (sel.picks.size() > 1) {
    ImGui::Text("Multi-selection: %d items", (int)sel.picks.size());

    std::vector<EntityID> ents;
    ents.reserve(sel.picks.size());
    for (uint32_t p : sel.picks) {
      EntityID e = sel.entityForPick(p);
      if (e == InvalidEntity) {
        e = engine.resolveEntityIndex(pickEntity(p));
      }
      if (e == InvalidEntity)
        continue;
      bool ok = true;
      for (EntityID q : ents)
        if (q == e) {
          ok = false;
          break;
        }
      if (ok)
        ents.push_back(e);
    }

    glm::vec3 delta{0, 0, 0};
    if (ImGui::DragFloat3("Move (delta)", &delta.x, 0.05f)) {
      for (EntityID e : ents) {
        if (!world.isAlive(e))
          continue;
        auto &t = world.transform(e);
        t.translation += delta;
        t.dirty = true;
        world.worldTransform(e).dirty = true;
      }
    }

    ImGui::End();
    engine.setPreviewMaterial(InvalidMaterial);
    return;
  }

  const uint32_t activePick =
      sel.activePick ? sel.activePick : sel.picks.back();
  EntityID e = sel.entityForPick(activePick);
  if (e == InvalidEntity)
    e = engine.resolveEntityIndex(pickEntity(activePick));
  const uint32_t sub = pickSubmesh(activePick);

  if (e == InvalidEntity || !world.isAlive(e)) {
    ImGui::TextUnformatted("Selection is invalid.");
    ImGui::End();
    return;
  }

  const auto &nm = world.name(e).name;
  ImGui::Text("Entity: %s", nm.c_str());

  ImGui::Separator();
  ImGui::Text("Active pick: 0x%08X", activePick);
  ImGui::Text("Submesh: %u", sub);
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (drawTransform(world, e, sequencer)) {
      engine.rebuildRenderables();
    }
  }

  if (world.hasMesh(e)) {
    if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
      drawMesh(world, sel, e, sub);
    }
  }

  if (world.hasCamera(e)) {
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
      auto &cam = world.ensureCamera(e);
      const auto &tr = world.transform(e);
      const bool camDisabled = tr.hidden || tr.hiddenEditor || tr.disabledAnim;
      int proj = (int)cam.projection;
      const char *projNames[] = {"Perspective", "Orthographic"};
      if (ImGui::Combo("Projection", &proj, projNames, 2)) {
        cam.projection = (CameraProjection)proj;
        cam.dirty = true;
      }

      if (cam.projection == CameraProjection::Perspective) {
        float fovDeg = cam.fovYDeg;
        if (ImGui::DragFloat("FOV (deg)", &fovDeg, 0.1f, 1.0f, 179.0f)) {
          cam.fovYDeg = fovDeg;
          cam.dirty = true;
        }
      } else {
        float orthoH = cam.orthoHeight;
        if (ImGui::DragFloat("Ortho Height", &orthoH, 0.1f, 0.01f, 100000.0f)) {
          cam.orthoHeight = (orthoH < 0.01f) ? 0.01f : orthoH;
          cam.dirty = true;
        }
      }
      if (ImGui::DragFloat("Near", &cam.nearZ, 0.01f, 0.0001f, 100.0f)) {
        cam.dirty = true;
      }
      if (ImGui::DragFloat("Far", &cam.farZ, 1.0f, 0.1f, 100000.0f)) {
        cam.dirty = true;
      }
      if (ImGui::DragFloat("Exposure", &cam.exposure, 0.05f, -20.0f, 20.0f)) {
        cam.dirty = true;
      }

      if (world.activeCamera() != e) {
        ImGui::BeginDisabled(camDisabled);
        if (ImGui::Button("Set Active Camera")) {
          world.setActiveCamera(e);
        }
        ImGui::EndDisabled();
      } else {
        ImGui::TextUnformatted("Active camera");
      }
    }
  }

  {
    static InspectorLight lightInspector;
    lightInspector.draw(world, sel);
  }

  MaterialHandle previewMat = InvalidMaterial;
  if (!world.hasLight(e) && world.hasMesh(e) && sub < world.submeshCount(e)) {
    static InspectorMaterial matInspector;
    matInspector.draw(engine, world.submesh(e, sub).material);
    previewMat = world.submesh(e, sub).material;

    if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
      glm::vec3 dir = engine.previewLightDir();
      if (ImGui::DragFloat3("Light Dir", &dir.x, 0.01f, -1.0f, 1.0f)) {
        if (glm::length(dir) < 1e-4f)
          dir = glm::vec3(0.0f, 1.0f, 0.0f);
        engine.previewLightDir() = glm::normalize(dir);
      }
      float intensity = engine.previewLightIntensity();
      if (ImGui::DragFloat("Light Intensity", &intensity, 0.05f, 0.0f, 100.0f)) {
        engine.previewLightIntensity() = std::max(0.0f, intensity);
      }
      float exposure = engine.previewLightExposure();
      if (ImGui::DragFloat("Light Exposure", &exposure, 0.05f, -10.0f, 10.0f)) {
        engine.previewLightExposure() = exposure;
      }

      const uint32_t tex = engine.renderer().previewTexture();
      if (tex != 0) {
        ImGui::Image((ImTextureID)(uintptr_t)tex, ImVec2(256, 256),
                     ImVec2(0, 1), ImVec2(1, 0));
      } else {
        ImGui::TextDisabled("Preview not available.");
      }
    }
  } else {
    ImGui::SeparatorText("Material");
    ImGui::TextDisabled("No mesh/submesh selected.");
  }
  engine.setPreviewMaterial(previewMat);

  ImGui::End();
}

} // namespace Nyx
