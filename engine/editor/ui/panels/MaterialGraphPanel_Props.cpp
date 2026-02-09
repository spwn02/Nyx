#include "MaterialGraphPanel.h"

#include "app/EngineContext.h"
#include "editor/graph/MaterialGraphSchema.h"
#include "editor/ui/UiPayloads.h"
#include "platform/FileDialogs.h"
#include "render/material/GpuMaterial.h"
#include "render/material/MaterialSystem.h"

#include <imgui.h>

#include <cstdint>

namespace Nyx {

namespace {

static uint32_t packSwizzle(uint8_t x, uint8_t y, uint8_t z, uint8_t w) {
  return uint32_t(x) | (uint32_t(y) << 8) | (uint32_t(z) << 16) |
         (uint32_t(w) << 24);
}

static void unpackSwizzle(uint32_t v, uint8_t &x, uint8_t &y, uint8_t &z,
                          uint8_t &w) {
  x = (uint8_t)(v & 0xFF);
  y = (uint8_t)((v >> 8) & 0xFF);
  z = (uint8_t)((v >> 16) & 0xFF);
  w = (uint8_t)((v >> 24) & 0xFF);
}

} // namespace

void MaterialGraphPanel::drawNodeProps(EngineContext &engine) {
  auto &materials = engine.materials();
  if (!materials.isAlive(m_mat))
    return;

  MaterialGraph &g = materials.graph(m_mat);

  ImGui::TextUnformatted("Properties");
  ImGui::Separator();

  if (m_selectedNode == 0) {
    ImGui::TextUnformatted("Select a node to edit properties.");
    return;
  }

  MatNode *n = nullptr;
  for (auto &node : g.nodes) {
    if (node.id == m_selectedNode) {
      n = &node;
      break;
    }
  }
  if (!n) {
    ImGui::TextUnformatted("Invalid selection.");
    return;
  }

  ImGui::Text("Node: %s", materialNodeName(n->type));
  ImGui::Separator();

  bool changed = false;

  switch (n->type) {
  case MatNodeType::ConstFloat: {
    changed |= ImGui::DragFloat("Value", &n->f.x, 0.01f, -10.0f, 10.0f);
  } break;
  case MatNodeType::ConstVec3: {
    changed |= ImGui::ColorEdit3("Value", &n->f.x);
  } break;
  case MatNodeType::ConstColor: {
    changed |= ImGui::ColorEdit3("Color", &n->f.x);
  } break;
  case MatNodeType::ConstVec4: {
    changed |= ImGui::ColorEdit4("Value", &n->f.x);
  } break;
  case MatNodeType::Texture2D:
  case MatNodeType::TextureMRA:
  case MatNodeType::NormalMap: {
    const bool isSRGB = (n->type == MatNodeType::Texture2D) && (n->u.y != 0);
    if (n->type == MatNodeType::Texture2D) {
      bool srgb = n->u.y != 0;
      if (ImGui::Checkbox("sRGB", &srgb)) {
        n->u.y = srgb ? 1u : 0u;
        if (!n->path.empty()) {
          uint32_t idx = materials.textures().getOrCreate2D(n->path, srgb);
          if (idx != TextureTable::Invalid)
            n->u.x = idx;
        }
        changed = true;
      }
    }

    if (ImGui::Button("Open...")) {
      const char *filters = "png,jpg,jpeg,tga,bmp,ktx,ktx2,hdr,exr,cube";
      if (auto path = FileDialogs::openFile("Open Texture", filters, nullptr)) {
        if (!path->empty()) {
          const bool wantSRGB =
              (n->type == MatNodeType::Texture2D) ? isSRGB : false;
          uint32_t idx = materials.textures().getOrCreate2D(*path, wantSRGB);
          if (idx != TextureTable::Invalid) {
            n->u.x = idx;
            n->path = *path;
            changed = true;
          }
        }
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
      n->u.x = kInvalidTexIndex;
      n->path.clear();
      changed = true;
    }

    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *pl =
              ImGui::AcceptDragDropPayload(UiPayload::TexturePath)) {
        const char *path = (const char *)pl->Data;
        if (path && path[0]) {
          const bool wantSRGB =
              (n->type == MatNodeType::Texture2D) ? isSRGB : false;
          uint32_t idx = materials.textures().getOrCreate2D(path, wantSRGB);
          if (idx != TextureTable::Invalid) {
            n->u.x = idx;
            n->path = path;
            changed = true;
          }
        }
      }
      ImGui::EndDragDropTarget();
    }
  } break;
  case MatNodeType::Swizzle: {
    uint8_t sx, sy, sz, sw;
    unpackSwizzle(n->u.x, sx, sy, sz, sw);
    const char *opts[] = {"X", "Y", "Z", "W"};
    int ix = (sx < 4) ? (int)sx : 0;
    int iy = (sy < 4) ? (int)sy : 1;
    int iz = (sz < 4) ? (int)sz : 2;
    int iw = (sw < 4) ? (int)sw : 3;
    if (ImGui::Combo("X", &ix, opts, 4) || ImGui::Combo("Y", &iy, opts, 4) ||
        ImGui::Combo("Z", &iz, opts, 4) || ImGui::Combo("W", &iw, opts, 4)) {
      n->u.x = packSwizzle((uint8_t)ix, (uint8_t)iy, (uint8_t)iz,
                           (uint8_t)iw);
      changed = true;
    }
  } break;
  case MatNodeType::Channel: {
    const char *opts[] = {"R", "G", "B", "A"};
    int ch = (n->u.x < 4) ? (int)n->u.x : 0;
    if (ImGui::Combo("Channel", &ch, opts, 4)) {
      n->u.x = (uint32_t)ch;
      changed = true;
    }
    ImGui::TextDisabled("Use with MRA to extract channels.");
  } break;
  default:
    ImGui::TextUnformatted("No editable properties for this node.");
    break;
  }

  if (changed) {
    materials.markGraphDirty(m_mat);
    materials.syncMaterialFromGraph(m_mat);
  }
}

} // namespace Nyx
