#include "InspectorPanel.h"
#include "../material/MaterialSystem.h"
#include "../scene/Pick.h"
#include <glm/glm.hpp>
#include <imgui.h>
#include "app/EngineContext.h"

namespace Nyx {

static void dragVec3(const char *label, glm::vec3 &v, float speed = 0.05f) {
  float tmp[3] = {v.x, v.y, v.z};
  if (ImGui::DragFloat3(label, tmp, speed)) {
    v = glm::vec3(tmp[0], tmp[1], tmp[2]);
  }
}

void InspectorPanel::draw(World &world, EngineContext &engine, Selection &sel) {
  ImGui::Begin("Inspector");

  if (sel.kind != SelectionKind::Picks || sel.picks.empty()) {
    ImGui::TextDisabled("Nothing selected");
    ImGui::End();
    return;
  }

  // Multi-select transform (relative)
  if (sel.picks.size() > 1) {
    ImGui::Text("Multi-selection: %d items", (int)sel.picks.size());

    // Compute unique entities from picks
    std::vector<EntityID> ents;
    ents.reserve(sel.picks.size());
    for (uint32_t p : sel.picks) {
      EntityID e =
          world.entityFromSlotIndex(pickEntity(p));
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
        t.translation += delta; // relative
        t.dirty = true;
        if (world.hasWorldTransform(e))
          world.worldTransform(e).dirty = true;
      }
    }

    ImGui::End();
    return;
  }

  // Single selection
  const uint32_t activePick =
      sel.activePick ? sel.activePick : sel.picks.front();
  const PickKey k = unpackPick(activePick);

  const EntityID entity = world.entityFromSlotIndex(k.entity);
  if (entity == InvalidEntity) {
    ImGui::TextDisabled("Selection points to dead entity");
    ImGui::End();
    return;
  }

  // Mesh
  {
    const uint32_t subCount = world.submeshCount(entity);
    ImGui::SeparatorText("Mesh");
    if (subCount == 0) {
      ImGui::TextDisabled("No submeshes");
    } else {
      ImGui::Text("Submeshes: %u", subCount);
      const uint32_t activeSub = k.submesh;
      if (activeSub < subCount) {
        const auto &mc = world.mesh(entity);
        const auto &sm = mc.submeshes[activeSub];
        ImGui::Text("Active submesh: %u (%s)", activeSub, sm.name.c_str());
      } else {
        ImGui::TextDisabled("Active submesh: %u (out of range)", activeSub);
      }
    }
  }

  // Transform
  {
    auto &t = world.transform(entity);

    ImGui::SeparatorText("Transform");
    glm::vec3 pos = t.translation;
    dragVec3("Position", pos);
    if (pos != t.translation) {
      t.translation = pos;
      t.dirty = true;
    }

    glm::vec3 scl = t.scale;
    dragVec3("Scale", scl, 0.02f);
    if (scl != t.scale) {
      t.scale = scl;
      t.dirty = true;
    }

    glm::vec3 rot = glm::eulerAngles(t.rotation);
    dragVec3("Rotation", rot, 0.02f);
    if (rot != glm::eulerAngles(t.rotation)) {
      t.rotation = glm::quat(rot);
      t.dirty = true;
    }
  }

  // Material (per submesh)
  ImGui::SeparatorText("Material");
  ImGui::Text("Entity: %u  Submesh: %u", (uint32_t)k.entity, k.submesh);

  // Phase-2A: map (entity,submesh) -> material handle/index
  auto mh = world.materialHandle(entity, k.submesh); // you implement this
  if (mh == InvalidMaterial || !engine.materials().isAlive(mh)) {
    MaterialData def{};
    mh = engine.materials().create(def);
    world.setMaterialHandle(entity, k.submesh, mh);
  }
  auto &mat = engine.materials().edit(
      mh); // returns CPU editable material (or GPU packed proxy)

  // Example PBR knobs
  float base[4] = {mat.baseColor.x, mat.baseColor.y,
                   mat.baseColor.z, mat.baseColor.w};
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

  ImGui::End();
}

} // namespace Nyx
