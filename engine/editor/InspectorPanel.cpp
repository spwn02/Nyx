#include "InspectorPanel.h"

#include "app/EngineContext.h"
#include "editor/InspectorLight.h"
#include "material/MaterialSystem.h"
#include "scene/Pick.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <imgui.h>
#include <string>

namespace Nyx {

static bool vec3Drag(const char *label, glm::vec3 &v, float speed = 0.05f) {
  return ImGui::DragFloat3(label, &v.x, speed);
}

static bool quatEditEulerDeg(const char *label, glm::quat &q) {
  glm::vec3 e = glm::degrees(glm::eulerAngles(q));
  bool changed = ImGui::DragFloat3(label, &e.x, 0.25f);
  if (changed) {
    glm::vec3 r = glm::radians(e);
    q = glm::normalize(glm::quat(r));
  }
  return changed;
}

static void drawTransform(World &world, EntityID e) {
  auto &tr = world.transform(e);

  bool changed = false;
  changed |= vec3Drag("Translation", tr.translation, 0.02f);
  changed |= quatEditEulerDeg("Rotation (deg)", tr.rotation);
  changed |= vec3Drag("Scale", tr.scale, 0.02f);

  if (changed) {
    tr.dirty = true;
    world.worldTransform(e).dirty = true;
  }

  if (ImGui::Button("Reset T")) {
    tr.translation = {0.0f, 0.0f, 0.0f};
    tr.dirty = true;
    world.worldTransform(e).dirty = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset R")) {
    tr.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    tr.dirty = true;
    world.worldTransform(e).dirty = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset S")) {
    tr.scale = {1.0f, 1.0f, 1.0f};
    tr.dirty = true;
    world.worldTransform(e).dirty = true;
  }
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

void InspectorPanel::draw(World &world, EngineContext &engine, Selection &sel) {
  ImGui::Begin("Inspector");

  if (sel.isEmpty()) {
    ImGui::TextUnformatted("No selection.");
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
    return;
  }

  const uint32_t activePick = sel.activePick ? sel.activePick : sel.picks.back();
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
    drawTransform(world, e);
  }

  if (world.hasMesh(e)) {
    if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
      drawMesh(world, sel, e, sub);
    }
  }

  if (world.hasCamera(e)) {
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
      auto &cam = world.ensureCamera(e);
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
        if (ImGui::Button("Set Active Camera")) {
          world.setActiveCamera(e);
        }
      } else {
        ImGui::TextUnformatted("Active camera");
      }
    }
  }

  {
    static InspectorLight lightInspector;
    lightInspector.draw(world, sel);
  }

  if (world.hasMesh(e)) {
    ImGui::SeparatorText("Material");
    auto &sm = world.submesh(e, sub);
    auto mh = sm.material;
    if (mh == InvalidMaterial || !engine.materials().isAlive(mh)) {
      MaterialData def{};
      mh = engine.materials().create(def);
      sm.material = mh;
    }
    auto &mat = engine.materials().edit(mh);

    float base[4] = {mat.baseColor.x, mat.baseColor.y, mat.baseColor.z,
                     mat.baseColor.w};
    if (ImGui::ColorEdit4("Base Color", base)) {
      mat.baseColor = {base[0], base[1], base[2], base[3]};
      engine.materials().markDirty(mh);
    }
    float mr[2] = {mat.metallic, mat.roughness};
    if (ImGui::DragFloat2("Metal/Rough", mr, 0.01f, 0.0f, 1.0f)) {
      mat.metallic = mr[0];
      mat.roughness = mr[1];
      engine.materials().markDirty(mh);
    }
  }

  ImGui::End();
}

} // namespace Nyx
