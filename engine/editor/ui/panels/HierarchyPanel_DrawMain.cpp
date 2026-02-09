#include "HierarchyPanel.h"
#include "app/EngineContext.h"
#include "core/Paths.h"
#include "editor/Selection.h"
#include "scene/EntityID.h"
#include <glad/glad.h>
#include "scene/Pick.h"
#include "scene/SelectionCycler.h"
#include "material/MaterialHandle.h"
#include "scene/material/MaterialData.h"
#include "editor/ui/UiPayloads.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <stb_image.h>
#include <stb_image_write.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace Nyx {
#include "HierarchyPanel_Helpers.inl"
void HierarchyPanel::draw(World &world, EntityID editorCamera,
                          EngineContext &engine, Selection &sel) {
  m_editorCamera = editorCamera;
  if (!m_iconInit) {
    m_iconInit = true;
    const std::filesystem::path iconDir = Paths::engineRes() / "icons";
    const std::filesystem::path jsonPath =
        Paths::engineRes() / "icon_atlas.json";
    const std::filesystem::path pngPath = Paths::engineRes() / "icon_atlas.png";
    if (std::filesystem::exists(jsonPath) && std::filesystem::exists(pngPath)) {
      m_iconReady = m_iconAtlas.loadFromJson(jsonPath.string());
    } else {
      m_iconReady = m_iconAtlas.buildFromFolder(
          iconDir.string(), jsonPath.string(), pngPath.string(), 64, 0);
    }
  }
  const uint64_t curPreviewHash = hashPreviewSettings(engine);
  if (curPreviewHash != m_matThumbSettingsHash) {
    for (auto &kv : m_matThumbs) {
      kv.second.ready = false;
      kv.second.pending = false;
      kv.second.saved = false;
    }
    m_matThumbSettingsHash = curPreviewHash;
  }
  ImGui::Begin("Hierarchy");
  m_visibleOrder.clear();
  // Click empty space to deselect
  if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() &&
      !ImGui::IsAnyItemHovered()) {
    sel.clear();
  }
  // Drop onto empty window space => make root (and clear category)
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload("NYX_ENTITY")) {
      EntityID dropped = *(const EntityID *)payload->Data;
      world.setParentKeepWorld(dropped, InvalidEntity);
      world.clearEntityCategories(dropped);
    }
    ImGui::EndDragDropTarget();
  }
  if (ImGui::BeginPopupContextWindow(
          "hier_ctx",
          ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
    if (ImGui::MenuItem("Add Entity")) {
      world.createEntity("Entity");
    }
    if (ImGui::MenuItem("Add Category")) {
      world.addCategory("Category");
    }
    if (m_copyEntity != InvalidEntity &&
        ImGui::MenuItem("Paste (Root)")) {
      EntityID dup =
          world.duplicateSubtree(m_copyEntity, InvalidEntity, &engine.materials());
      if (dup != InvalidEntity)
        sel.setSinglePick(packPick(dup, 0), dup);
    }
    if (ImGui::MenuItem("Unisolate All")) {
      unisolateAll(world, m_editorCamera);
    }
    ImGui::EndPopup();
  }
  if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto &ms = engine.materials();
    const uint32_t count = ms.slotCount();
    static uint64_t s_editMat = 0;
    static char s_editBuf[128]{};
    const ImVec2 matStart = ImGui::GetCursorScreenPos();
    auto matKey = [](MaterialHandle h) -> uint64_t {
      return (uint64_t(h.slot) << 32) | uint64_t(h.gen);
    };
    for (uint32_t i = 0; i < count; ++i) {
      MaterialHandle h = ms.handleBySlot(i);
      if (!ms.isAlive(h))
        continue;
      const MaterialData &md = ms.cpu(h);
      std::string label = md.name;
      if (label.empty())
        label = "Material " + std::to_string(i);
      const uint64_t key = matKey(h);
      MatThumb &th = getMaterialThumb(engine, h);
      ImGui::PushID((int)i);
      if (th.tex != 0) {
        const ImVec2 iconSize(16.0f, 16.0f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##mat_thumb", iconSize);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float radius = iconSize.x * 0.5f;
        dl->AddImageRounded((ImTextureID)(uintptr_t)th.tex, p,
                            ImVec2(p.x + iconSize.x, p.y + iconSize.y),
                            ImVec2(0, 1), ImVec2(1, 0),
                            IM_COL32(255, 255, 255, 255), radius);
        dl->AddCircle(ImVec2(p.x + radius, p.y + radius), radius,
                      IM_COL32(255, 255, 255, 40), 0, 1.0f);
      }
      ImGui::SameLine();
      if (s_editMat == key) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGuiInputTextFlags f = ImGuiInputTextFlags_EnterReturnsTrue |
                                ImGuiInputTextFlags_AutoSelectAll;
        const bool commit =
            ImGui::InputText("##mat_name", s_editBuf, sizeof(s_editBuf), f);
        if (commit || ImGui::IsItemDeactivatedAfterEdit()) {
          ms.cpu(h).name = s_editBuf;
          s_editMat = 0;
        }
      } else {
        const bool selected =
            (sel.kind == SelectionKind::Material && sel.activeMaterial == h);
        if (ImGui::Selectable(label.c_str(), selected)) {
          sel.setMaterial(h);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
          s_editMat = key;
          std::snprintf(s_editBuf, sizeof(s_editBuf), "%s", label.c_str());
          ImGui::SetKeyboardFocusHere(-1);
        }
      }
      char matItemCtx[64];
      std::snprintf(matItemCtx, sizeof(matItemCtx), "mat_item_ctx##%llu",
                    (unsigned long long)key);
      if (ImGui::BeginPopupContextItem(matItemCtx)) {
        if (ImGui::MenuItem("Rename")) {
          s_editMat = key;
          std::snprintf(s_editBuf, sizeof(s_editBuf), "%s", label.c_str());
          ImGui::SetKeyboardFocusHere(-1);
        }
        if (ImGui::MenuItem("Duplicate")) {
          MaterialData copy = ms.cpu(h);
          MaterialHandle nh = ms.create(copy);
          sel.setMaterial(nh);
        }
        if (ImGui::MenuItem("Delete")) {
          clearMaterialFromWorld(world, h);
          ms.destroy(h);
          if (sel.kind == SelectionKind::Material &&
              sel.activeMaterial == h) {
            sel.clear();
          }
          ImGui::EndPopup();
          ImGui::PopID();
          continue;
        }
        ImGui::EndPopup();
      }
      beginMaterialDragSource(h, label.c_str());
      ImGui::PopID();
    }
    ImVec2 matEnd = ImGui::GetCursorScreenPos();
    if (matEnd.y < matStart.y + 40.0f)
      matEnd.y = matStart.y + 40.0f;
    if (ImGui::IsMouseHoveringRect(matStart, matEnd, false) &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        !ImGui::IsAnyItemHovered()) {
      ImGui::OpenPopup("mat_empty_ctx");
    }
    if (ImGui::BeginPopup("mat_empty_ctx")) {
      if (ImGui::MenuItem("Add Material")) {
        MaterialData md{};
        md.name = "Material " + std::to_string(ms.slotCount());
        MaterialHandle nh = ms.create(md);
        sel.setMaterial(nh);
      }
      ImGui::EndPopup();
    }
  }
  ImGui::Separator();
  auto collectSelectedEntities = [&]() {
    std::vector<EntityID> ents;
    ents.reserve(sel.picks.size());
    std::unordered_set<uint64_t> seen;
    seen.reserve(sel.picks.size());

    for (uint32_t p : sel.picks) {
      EntityID e = sel.entityForPick(p);
      if (e == InvalidEntity)
        e = engine.resolveEntityIndex(pickEntity(p));
      if (e == InvalidEntity || !world.isAlive(e))
        continue;

      const uint64_t key = (uint64_t(e.generation) << 32) | uint64_t(e.index);
      if (seen.insert(key).second)
        ents.push_back(e);
    }
    return ents;
  };
  if (ImGui::CollapsingHeader("Categories",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    static char newCat[64]{};
    static uint32_t editCat = UINT32_MAX;
    static char editBuf[128]{};
    // Drop target on empty space => move category to root, or remove entity from category
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *p =
              ImGui::AcceptDragDropPayload("NYX_CATEGORY")) {
        uint32_t dropped = *(const uint32_t *)p->Data;
        world.setCategoryParent(dropped, -1);
      }
      if (const ImGuiPayload *p =
              ImGui::AcceptDragDropPayload("NYX_ENTITY")) {
        EntityID dropped = *(const EntityID *)p->Data;
      world.clearEntityCategories(dropped);
      }
      ImGui::EndDragDropTarget();
    }
    const auto &cats = world.categories();
    std::function<void(uint32_t)> drawCategory = [&](uint32_t ci) {
      const auto &cat = cats[ci];
      ImGui::PushID((int)ci);
      ImGuiTreeNodeFlags cflags =
          ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
      if (cat.children.empty())
        cflags |= ImGuiTreeNodeFlags_Leaf;
      ImGui::SetNextItemAllowOverlap();
      const bool open = ImGui::TreeNodeEx("##cat", cflags, "%s",
                                          cat.name.c_str());
      // Inline rename (double-click)
      if (ImGui::IsItemHovered() &&
          ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        editCat = ci;
        std::snprintf(editBuf, sizeof(editBuf), "%s", cat.name.c_str());
      }
      if (editCat == ci) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputText("##RenameCat", editBuf, sizeof(editBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue) ||
            ImGui::IsItemDeactivatedAfterEdit()) {
          world.renameCategory(ci, editBuf);
          editCat = UINT32_MAX;
        }
      }
      char catCtx[64];
      std::snprintf(catCtx, sizeof(catCtx), "cat_ctx##%u", ci);
      if (ImGui::BeginPopupContextItem(catCtx)) {
        if (ImGui::MenuItem("Add Subcategory")) {
          const uint32_t idx = world.addCategory("Category");
          world.setCategoryParent(idx, (int32_t)ci);
        }
        if (ImGui::MenuItem("Add Entity")) {
          EntityID ne = world.createEntity("Entity");
          world.addEntityCategory(ne, (int32_t)ci);
        }
        if (ImGui::MenuItem("Select All")) {
          selectEntities(world, sel, cat.entities);
        }
        if (ImGui::MenuItem("Rename")) {
          editCat = ci;
          std::snprintf(editBuf, sizeof(editBuf), "%s", cat.name.c_str());
        }
        if (ImGui::MenuItem("Delete")) {
          world.removeCategory(ci);
          if (open)
            ImGui::TreePop();
          ImGui::PopID();
          ImGui::EndPopup();
          return;
        }
        ImGui::EndPopup();
      }
      // Drag source for category
      if (ImGui::BeginDragDropSource()) {
        uint32_t payload = ci;
        ImGui::SetDragDropPayload("NYX_CATEGORY", &payload, sizeof(payload));
        ImGui::TextUnformatted(cat.name.c_str());
        ImGui::EndDragDropSource();
      }
      // Drop target for entity/category
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *p =
                ImGui::AcceptDragDropPayload("NYX_ENTITY")) {
          EntityID dropped = *(const EntityID *)p->Data;
          ImGuiIO &io = ImGui::GetIO();
          auto ents = collectSelectedEntities();
          const bool inSelection =
              std::find(ents.begin(), ents.end(), dropped) != ents.end();
          if (io.KeyCtrl) {
            for (EntityID e : ents)
              world.addEntityCategory(e, (int32_t)ci);
          } else {
            if (inSelection && ents.size() > 1) {
              for (EntityID e : ents) {
                world.clearEntityCategories(e);
                world.addEntityCategory(e, (int32_t)ci);
              }
            } else {
              world.clearEntityCategories(dropped);
              world.addEntityCategory(dropped, (int32_t)ci);
            }
          }
        }
        if (const ImGuiPayload *p =
                ImGui::AcceptDragDropPayload("NYX_CATEGORY")) {
          uint32_t dropped = *(const uint32_t *)p->Data;
          if (dropped != ci)
            world.setCategoryParent(dropped, (int32_t)ci);
        }
        ImGui::EndDragDropTarget();
      }
      // Buttons
      ImGui::SameLine();
      if (ImGui::SmallButton("Assign")) {
        auto ents = collectSelectedEntities();
        for (EntityID e : ents)
          world.addEntityCategory(e, (int32_t)ci);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Remove")) {
        world.removeCategory(ci);
        if (open)
          ImGui::TreePop();
        ImGui::PopID();
        return;
      }
      if (open) {
        for (EntityID e : cat.entities) {
          if (!world.isAlive(e))
            continue;
          if (m_iconReady) {
            // ensure icon atlas loaded for category rows too
          }
          const auto &nm = world.name(e).name;
          ImGui::PushID((void *)treeId(e));
          const bool isSelected = sel.hasPick(packPick(e, 0));
          if (m_renameEntity == e) {
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("##RenameEnt", m_renameEntityBuf.data(),
                                 m_renameEntityBuf.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue) ||
                ImGui::IsItemDeactivatedAfterEdit()) {
              world.setName(e, m_renameEntityBuf.data());
              m_renameEntity = InvalidEntity;
            }
          } else {
            if (ImGui::Selectable(nm.c_str(), isSelected)) {
              sel.setSinglePick(packPick(e, 0), e);
            }
          }
          // Icon in categories
          const AtlasRegion *iconReg = nullptr;
          ImU32 iconTint = IM_COL32(188, 128, 78, 255);
          if (m_iconReady) {
            if (world.hasCamera(e)) {
              iconReg = m_iconAtlas.find("camera");
            } else if (world.hasMesh(e)) {
              iconReg = m_iconAtlas.find("object");
            }
          }
          if (iconReg) {
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const float frameH = ImGui::GetFrameHeight();
            const float iconSize = std::min(16.0f, std::max(8.0f, frameH - 2.0f));
            const float iconY = itemMin.y + (frameH - iconSize) * 0.5f - 2.0f;
            DrawAtlasIconAt(m_iconAtlas, *iconReg,
                            ImVec2(itemMin.x + 4.0f, iconY),
                            ImVec2(iconSize, iconSize), iconTint);
          }
          if (ImGui::IsItemHovered() &&
              ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_renameEntity = e;
            std::snprintf(m_renameEntityBuf.data(), m_renameEntityBuf.size(), "%s",
                          nm.c_str());
          }
      char catEntCtx[64];
      std::snprintf(catEntCtx, sizeof(catEntCtx), "cat_ent_ctx##%llu",
                    (unsigned long long)treeId(e));
      if (ImGui::BeginPopupContextItem(catEntCtx)) {
        if (ImGui::MenuItem("Rename")) {
          m_renameEntity = e;
          std::snprintf(m_renameEntityBuf.data(), m_renameEntityBuf.size(), "%s",
                        nm.c_str());
        }
        if (ImGui::MenuItem("Focus")) {
          sel.focusEntity = e;
        }
        if (ImGui::MenuItem("Copy")) {
          m_copyEntity = e;
        }
        if (ImGui::MenuItem("Duplicate")) {
          EntityID parent = world.parentOf(e);
          EntityID dup = world.duplicateSubtree(e, parent, &engine.materials());
          if (dup != InvalidEntity) {
            sel.setSinglePick(packPick(dup, 0), dup);
          }
        }
        if (m_copyEntity != InvalidEntity &&
            ImGui::MenuItem("Paste (Sibling)")) {
          EntityID parent = world.parentOf(e);
          EntityID dup =
              world.duplicateSubtree(m_copyEntity, parent, &engine.materials());
          if (dup != InvalidEntity)
            sel.setSinglePick(packPick(dup, 0), dup);
        }
        if (m_copyEntity != InvalidEntity &&
            ImGui::MenuItem("Paste (Child)")) {
          EntityID dup =
              world.duplicateSubtree(m_copyEntity, e, &engine.materials());
          if (dup != InvalidEntity)
            sel.setSinglePick(packPick(dup, 0), dup);
        }
        if (ImGui::MenuItem("Isolate")) {
          isolateEntity(world, e, m_editorCamera);
        }
        if (ImGui::MenuItem("Reset Transform")) {
          resetTransform(world, e);
        }
        if (ImGui::MenuItem("Copy Transform")) {
          copyTransform(world, e);
        }
        if (ImGui::MenuItem("Paste Transform", nullptr, false,
                            m_hasCopiedTransform)) {
          pasteTransform(world, e);
        }
        if (ImGui::MenuItem("Delete")) {
          world.destroyEntity(e);
          sel.removePicksForEntity(e);
        }
        ImGui::EndPopup();
          }
          if (ImGui::BeginDragDropSource()) {
            EntityID payload = e;
            ImGui::SetDragDropPayload("NYX_ENTITY", &payload, sizeof(payload));
            ImGui::TextUnformatted(nm.c_str());
            ImGui::EndDragDropSource();
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("X"))
            world.removeEntityCategory(e, (int32_t)ci);
          ImGui::PopID();
        }
        for (uint32_t child : cat.children) {
          if (child < cats.size())
            drawCategory(child);
        }
        ImGui::TreePop();
      }
      ImGui::PopID();
    };
    for (uint32_t ci = 0; ci < (uint32_t)cats.size(); ++ci) {
      if (cats[ci].parent != -1)
        continue;
      drawCategory(ci);
    }
  }
  for (EntityID e : m_roots) {
    if (e == editorCamera)
      continue;
    if (world.entityCategories(e))
      continue;
    drawEntityNode(world, e, engine, sel);
  }
  ImGui::Dummy(ImVec2(0.0f, 200.0f));
  ImGui::End();
}
} // namespace Nyx
