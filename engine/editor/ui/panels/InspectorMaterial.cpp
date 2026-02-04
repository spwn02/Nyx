#include "InspectorMaterial.h"

#include "glm/fwd.hpp"
#include "material/MaterialHandle.h"
#include "platform/FileDialogs.h"

#include "render/material/MaterialSystem.h"
#include "scene/material/MaterialData.h"
#include "scene/material/MaterialTypes.h"

#include <glad/glad.h>
#include <imgui.h>

namespace Nyx {

static bool imguiVec3(const char *label, glm::vec3 &vec) {
  return ImGui::ColorEdit3(label, &vec.x);
}

static bool imguiVec4(const char *label, glm::vec4 &vec) {
  return ImGui::ColorEdit4(label, &vec.x);
}

void InspectorMaterial::draw(MaterialSystem &mats, MaterialHandle h) {
  if (!mats.isAlive(h)) {
    return;
  }

  MaterialData &m = mats.cpu(h);

  if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  bool dirty = false;

  // Factors
  glm::vec4 bc = m.baseColorFactor;
  if (imguiVec4("Base Color", bc)) {
    m.baseColorFactor = bc;
    dirty = true;
  }

  glm::vec3 em = m.emissiveFactor;
  if (imguiVec3("Emissive", em)) {
    m.emissiveFactor = em;
    dirty = true;
  }

  dirty |= ImGui::SliderFloat("Metallic", &m.metallic, 0.0f, 1.0f);
  dirty |= ImGui::SliderFloat("Roughness", &m.roughness, 0.0f, 1.0f);
  dirty |= ImGui::SliderFloat("AO", &m.ao, 0.0f, 1.0f);

  // UV
  dirty |= ImGui::SliderFloat2("UV Scale", &m.uvScale.x, 0.0f, 100.0f, "%.3f");
  dirty |=
      ImGui::SliderFloat2("UV Offset", &m.uvOffset.x, -100.0f, 100.0f, "%.3f");

  ImGui::SeparatorText("Textures");

  // Slots
  drawTextureSlot(mats, h, "Base Color (sRGB)",
                  static_cast<uint32_t>(MaterialTexSlot::BaseColor), true);
  drawTextureSlot(mats, h, "Metallic-Roughness (Linear)",
                  static_cast<uint32_t>(MaterialTexSlot::MetalRough), false);
  drawTextureSlot(mats, h, "Normal Map (Linear)",
                  static_cast<uint32_t>(MaterialTexSlot::Normal), false);
  drawTextureSlot(mats, h, "Emissive (sRGB)",
                  static_cast<uint32_t>(MaterialTexSlot::Emissive), true);
  drawTextureSlot(mats, h, "Ambient Occlusion (Linear)",
                  static_cast<uint32_t>(MaterialTexSlot::AO), false);

  if (dirty) {
    mats.markDirty(h);
  }
}

void InspectorMaterial::drawTextureSlot(MaterialSystem &mats, MaterialHandle h,
                                        const char *label, uint32_t slotIndex,
                                        bool srgb) {
  MaterialData &m = mats.cpu(h);

  ImGui::PushID(static_cast<int>(slotIndex));

  ImGui::TextUnformatted(label);

  // Current path (short)
  const std::string &path = m.texPath[slotIndex];
  if (!path.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", path.c_str());
  }

  // Buttons
  if (ImGui::Button("Choose...")) {
    // Common texture formats
    auto p = FileDialogs::openFile(
        "Select Texture", "png,jpg,jpeg,tga,bmp,ktx,ktx2,hdr,exr", nullptr);
    if (p.has_value()) {
      m.texPath[slotIndex] = *p;

      // Preload into table so preview works immediately.
      mats.textures().getOrCreate2D(m.texPath[slotIndex], srgb);

      mats.markDirty(h);
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    if (!m.texPath[slotIndex].empty()) {
      m.texPath[slotIndex].clear();
      mats.markDirty(h);
    }
  }

  // Preview (if available in table)
  if (!m.texPath[slotIndex].empty()) {
    const uint32_t texIndex =
        mats.textures().getOrCreate2D(m.texPath[slotIndex], srgb);
    if (texIndex != 0xFFFFFFFF) {
      const auto &gl = mats.textures().glTextures();
      if (texIndex < gl.size() && gl[texIndex] != 0) {
        ImGui::SameLine();
        const ImVec2 sz(48.0f, 48.0f);
        ImGui::Image(
            reinterpret_cast<void *>(static_cast<uintptr_t>(gl[texIndex])), sz);
      }
    }
  }

  ImGui::PopID();
}

} // namespace Nyx
