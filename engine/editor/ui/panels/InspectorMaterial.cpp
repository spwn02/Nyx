#include "InspectorMaterial.h"

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

bool InspectorMaterial::acceptTexturePathDrop(std::string &outAbsPath) {
  if (!ImGui::BeginDragDropTarget())
    return false;

  const ImGuiPayload *p = ImGui::AcceptDragDropPayload("NYX_ASSET_PATH_TEX");
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

void InspectorMaterial::draw(MaterialSystem &materials, MaterialHandle &handle) {
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

  bool changed = false;

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

  ImGui::Separator();

  changed |= drawSlot(materials, handle, MaterialTexSlot::BaseColor);
  changed |= drawSlot(materials, handle, MaterialTexSlot::Normal);
  changed |= drawSlot(materials, handle, MaterialTexSlot::Metallic);
  changed |= drawSlot(materials, handle, MaterialTexSlot::Roughness);
  changed |= drawSlot(materials, handle, MaterialTexSlot::AO);
  changed |= drawSlot(materials, handle, MaterialTexSlot::Emissive);

  if (changed) {
    materials.uploadIfDirty();
  }
}

} // namespace Nyx
