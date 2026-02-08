#include "InspectorMaterial.h"

#include "app/EngineContext.h"
#include "editor/ui/UiPayloads.h"
#include "platform/FileDialogs.h"
#include "render/material/MaterialSystem.h"
#include "render/material/MaterialTexturePolicy.h"
#include "render/material/TextureTable.h"
#include "scene/material/MaterialData.h"
#include <imgui.h>

namespace Nyx {

static ImTextureID toImTex(uint32_t glTex) {
  return (ImTextureID)(intptr_t)glTex;
}

static void drawMaterialValidation(const MaterialData &m) {
  const MaterialValidation v = validateMaterial(m);
  if (!v.ok) {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1, 0.25f, 0.25f, 1), "Material Error:");
    ImGui::TextWrapped("%s", v.message.c_str());
    return;
  }
  if (v.warn && !v.message.empty()) {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1, 0.75f, 0.25f, 1), "Material Warning:");
    ImGui::TextWrapped("%s", v.message.c_str());
  }
}

bool InspectorMaterial::acceptTexturePathDrop(std::string &outAbsPath) {
  if (!ImGui::BeginDragDropTarget())
    return false;

  const ImGuiPayload *p =
      ImGui::AcceptDragDropPayload(UiPayload::TexturePath);
  if (p && p->Data && p->DataSize > 0) {
    const char *str = (const char *)p->Data;
    outAbsPath = std::string(str);
    ImGui::EndDragDropTarget();
    return true;
  }

  ImGui::EndDragDropTarget();
  return false;
}

bool InspectorMaterial::assignSlotFromPath(MaterialSystem &materials,
                                           MaterialHandle handle,
                                           MaterialTexSlot slot,
                                           const std::string &absPath) {
  if (!materials.isAlive(handle))
    return false;

  auto &mat = materials.cpu(handle);
  mat.texPath[static_cast<size_t>(slot)] = absPath;
  materials.markDirty(handle);

  const bool wantSRGB = materialSlotWantsSRGB(slot);
  materials.textures().getOrCreate2D(absPath, wantSRGB);
  return true;
}

bool InspectorMaterial::clearSlot(MaterialSystem &materials,
                                  MaterialHandle handle,
                                  MaterialTexSlot slot) {
  if (!materials.isAlive(handle))
    return false;
  auto &mat = materials.cpu(handle);
  mat.texPath[static_cast<size_t>(slot)].clear();
  materials.markDirty(handle);
  return true;
}

bool InspectorMaterial::reloadSlot(MaterialSystem &materials,
                                   MaterialHandle handle,
                                   MaterialTexSlot slot) {
  if (!materials.isAlive(handle))
    return false;
  auto &mat = materials.cpu(handle);
  const std::string &path = mat.texPath[static_cast<size_t>(slot)];
  if (path.empty())
    return false;

  const bool wantSRGB = materialSlotWantsSRGB(slot);
  uint32_t idx = materials.textures().getOrCreate2D(path, wantSRGB);
  if (idx != TextureTable::Invalid) {
    materials.textures().reloadByIndex(idx);
    materials.markDirty(handle);
    return true;
  }
  return false;
}

bool InspectorMaterial::drawSlot(MaterialSystem &materials,
                                 MaterialHandle handle,
                                 MaterialTexSlot slot) {
  if (!materials.isAlive(handle))
    return false;

  auto &mat = materials.cpu(handle);
  const std::string &path = mat.texPath[static_cast<size_t>(slot)];
  const bool wantSRGB = materialSlotWantsSRGB(slot);
  bool changed = false;

  uint32_t glTex = 0;
  uint32_t texIndex = TextureTable::Invalid;
  if (!path.empty()) {
    texIndex = materials.textures().getOrCreate2D(path, wantSRGB);
    if (texIndex != TextureTable::Invalid)
      glTex = materials.textures().glTexByIndex(texIndex);
  }

  ImGui::PushID((int)slot);

  ImGui::TextUnformatted(materialSlotName(slot));
  ImGui::SameLine();
  ImGui::TextDisabled(wantSRGB ? "[sRGB]" : "[Linear]");

  const float thumb = 72.0f;
  if (glTex != 0) {
    ImGui::Image(toImTex(glTex), ImVec2(thumb, thumb));
  } else {
    ImGui::Button("##empty", ImVec2(thumb, thumb));
  }

  std::string dropPath;
  if (acceptTexturePathDrop(dropPath)) {
    changed |= assignSlotFromPath(materials, handle, slot, dropPath);
  }

  ImGui::SameLine();
  ImGui::BeginGroup();
  {
    if (!path.empty()) {
      ImGui::TextWrapped("%s", path.c_str());
    } else {
      ImGui::TextDisabled("No texture");
    }

    SlotBinding binding{};
    binding.path = path;
    binding.requestedSRGB = wantSRGB;
    binding.texIndex = texIndex;
    const SlotIssue issue = validateSlot(slot, binding);
    if (issue.kind != SlotIssueKind::None) {
      ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Warning: %s",
                         issue.message.c_str());
    }

    if (ImGui::Button("Open...")) {
      const char *filters = "png,jpg,jpeg,tga,bmp,ktx,ktx2,hdr,exr";
      auto chosen =
          FileDialogs::openFile(materialSlotName(slot), filters, nullptr);
      if (chosen.has_value()) {
        changed |= assignSlotFromPath(materials, handle, slot, chosen.value());
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
      changed |= clearSlot(materials, handle, slot);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
      changed |= reloadSlot(materials, handle, slot);
    }
  }
  ImGui::EndGroup();

  ImGui::PopID();
  ImGui::Separator();
  return changed;
}

void InspectorMaterial::draw(EngineContext &engine, MaterialHandle &handle) {
  auto &materials = engine.materials();

  ImGui::SeparatorText("Material");

  if (!materials.isAlive(handle)) {
    ImGui::TextDisabled("No material selected.");
    if (ImGui::Button("Create Material")) {
      MaterialData def{};
      handle = materials.create(def);
    }
    return;
  }

  auto &mat = materials.cpu(handle);
  auto &graph = materials.graph(handle);

  bool changed = false;

  if (ImGui::CollapsingHeader("Material Graph",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    int mode = static_cast<int>(graph.alphaMode);
    const char *items[] = {"Opaque", "Mask", "Blend"};
    if (ImGui::Combo("Alpha Mode", &mode, items, 3)) {
      graph.alphaMode = static_cast<MatAlphaMode>(mode);
      mat.alphaMode = graph.alphaMode;
      materials.markGraphDirty(handle);
      materials.markDirty(handle);
    }
    if (graph.alphaMode == MatAlphaMode::Mask) {
      if (ImGui::SliderFloat("Alpha Cutoff", &graph.alphaCutoff, 0.0f, 1.0f)) {
        mat.alphaCutoff = graph.alphaCutoff;
        materials.markGraphDirty(handle);
        materials.markDirty(handle);
      }
    }
    if (graph.alphaMode == MatAlphaMode::Blend) {
      ImGui::TextColored(ImVec4(1, 0.75f, 0.25f, 1),
                         "Blend is rendered in Transparent pass (no ID write).");
    }

    const std::string &err = materials.graphError(handle);
    if (!err.empty()) {
      ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Graph Error: %s",
                         err.c_str());
    } else {
      ImGui::TextDisabled("Graph OK");
    }

    ImGui::TextDisabled("Graph is shown in the Material Graph panel.");
  }

  float base[4] = {mat.baseColorFactor.x, mat.baseColorFactor.y,
                   mat.baseColorFactor.z, mat.baseColorFactor.w};
  if (ImGui::ColorEdit4("Base Color", base)) {
    mat.baseColorFactor = {base[0], base[1], base[2], base[3]};
    materials.markDirty(handle);
    changed = true;
  }

  float emi[3] = {mat.emissiveFactor.x, mat.emissiveFactor.y,
                 mat.emissiveFactor.z};
  if (ImGui::ColorEdit3("Emissive", emi)) {
    mat.emissiveFactor = {emi[0], emi[1], emi[2]};
    materials.markDirty(handle);
    changed = true;
  }

  float mr[2] = {mat.metallic, mat.roughness};
  if (ImGui::DragFloat2("Metal/Rough", mr, 0.01f, 0.0f, 1.0f)) {
    mat.metallic = mr[0];
    mat.roughness = mr[1];
    materials.markDirty(handle);
    changed = true;
  }

  if (ImGui::DragFloat("AO", &mat.ao, 0.01f, 0.0f, 1.0f)) {
    materials.markDirty(handle);
    changed = true;
  }

  float uvScale[2] = {mat.uvScale.x, mat.uvScale.y};
  if (ImGui::DragFloat2("UV Scale", uvScale, 0.01f, 0.0f, 100.0f)) {
    mat.uvScale = {uvScale[0], uvScale[1]};
    materials.markDirty(handle);
    changed = true;
  }

  float uvOffset[2] = {mat.uvOffset.x, mat.uvOffset.y};
  if (ImGui::DragFloat2("UV Offset", uvOffset, 0.01f, -100.0f, 100.0f)) {
    mat.uvOffset = {uvOffset[0], uvOffset[1]};
    materials.markDirty(handle);
    changed = true;
  }

  if (ImGui::Checkbox("Tangent-Space Normal", &mat.tangentSpaceNormal)) {
    materials.markDirty(handle);
    changed = true;
  }

  ImGui::Separator();

  changed |= drawSlot(materials, handle, MaterialTexSlot::BaseColor);
  changed |= drawSlot(materials, handle, MaterialTexSlot::Normal);
  changed |= drawSlot(materials, handle, MaterialTexSlot::Metallic);
  changed |= drawSlot(materials, handle, MaterialTexSlot::Roughness);
  changed |= drawSlot(materials, handle, MaterialTexSlot::AO);
  changed |= drawSlot(materials, handle, MaterialTexSlot::Emissive);

  static MaterialData s_clipboard{};
  static bool s_hasClipboard = false;

  ImGui::Separator();
  if (ImGui::Button("Copy Material")) {
    s_clipboard = mat;
    s_hasClipboard = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Paste Material") && s_hasClipboard) {
    mat = s_clipboard;
    materials.markDirty(handle);
    changed = true;
  }

  drawMaterialValidation(mat);

  if (changed) {
    materials.syncGraphFromMaterial(handle, true);
    materials.uploadIfDirty();
  }

}

} // namespace Nyx
