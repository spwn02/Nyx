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
#include <vector>
namespace Nyx {
#include "HierarchyPanel_Helpers.inl"

void HierarchyPanel::drawEntityNode(World &world, EntityID e,
                                    EngineContext &engine, Selection &sel) {
  if (!world.isAlive(e))
    return;
  m_visibleOrder.push_back(e);
  const auto &nm = world.name(e).name;
  const bool hasMesh = world.hasMesh(e);
  const uint32_t subCount = hasMesh ? world.submeshCount(e) : 0;
  const bool hasSubmeshes = subCount > 0;
  uint32_t storedSubmeshes = 0;
  if (hasMesh)
    storedSubmeshes = (uint32_t)world.mesh(e).submeshes.size();
  const AtlasRegion *iconReg = nullptr;
  ImU32 iconTint = IM_COL32(188, 128, 78, 255);
  if (m_iconReady) {
    if (world.hasCamera(e)) {
      iconReg = m_iconAtlas.find("camera");
    } else if (world.hasMesh(e)) {
      iconReg = m_iconAtlas.find("object");
    }
  }
  const bool hasChildren = (world.hierarchy(e).firstChild != InvalidEntity);
  const bool hasTreeContent = hasChildren || hasSubmeshes;
  const bool isSelected =
      isEntityHighlightedByPicks(sel, e, std::max(1u, subCount));
  ImGuiTreeNodeFlags flags =
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
  if (isSelected)
    flags |= ImGuiTreeNodeFlags_Selected;
  if (!hasTreeContent)
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  char label[256];
  if (world.hasMesh(e)) {
    auto &mc = world.mesh(e);
    ProcMeshType t = ProcMeshType::Cube;
    if (!mc.submeshes.empty())
      t = mc.submeshes[0].type;
    std::snprintf(label, sizeof(label), "%s  [%s]", nm.c_str(),
                  meshTypeName(t));
  } else {
    std::snprintf(label, sizeof(label), "%s", nm.c_str());
  }
  const float frameH = ImGui::GetFrameHeight();
  const float iconSize = std::min(16.0f, std::max(8.0f, frameH - 2.0f));
  const float iconGap = 4.0f;
  std::string paddedLabel;
  if (iconReg) {
    const float spaceW = ImGui::CalcTextSize(" ").x;
    const float padWidth = iconSize + iconGap;
    const int padSpaces = (int)std::ceil(padWidth / spaceW);
    paddedLabel.assign((size_t)padSpaces, ' ');
    paddedLabel += label;
  }
  const char *drawLabel = iconReg ? paddedLabel.c_str() : label;
  const bool open =
      ImGui::TreeNodeEx((void *)treeId(e), flags, "%s", drawLabel);
  if (iconReg) {
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const float labelStartX = itemMin.x + ImGui::GetTreeNodeToLabelSpacing();
    const float iconY = itemMin.y + (frameH - iconSize) * 0.5f - 2.0f;
    DrawAtlasIconAt(m_iconAtlas, *iconReg, ImVec2(labelStartX, iconY),
                    ImVec2(iconSize, iconSize), iconTint);
  }
  // ENTITY click selection
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    ImGuiIO &io = ImGui::GetIO();
    const bool ctrl = io.KeyCtrl;
    const bool shift = io.KeyShift;
    const EntityID anchor =
        (sel.kind == SelectionKind::Picks) ? sel.activeEntity : InvalidEntity;
    if (shift && anchor != InvalidEntity) {
      rangeSelectEntities(world, sel, m_visibleOrder, anchor, e);
    } else if (ctrl) {
      toggleEntity(world, sel, e);
    } else {
      std::vector<CycleTarget> targets;
      buildCycleTargets(world, e, targets, true);
      if (!targets.empty()) {
        uint32_t &idx = sel.cycleIndexByEntity[e];
        if (idx >= (uint32_t)targets.size())
          idx = 0;
        const CycleTarget t = targets[idx];
        idx = (idx + 1u) % (uint32_t)targets.size();
        const uint32_t pid = packPick(t.entity, t.submesh);
        sel.setSinglePick(pid, t.entity);
      } else {
        setSingleEntity(world, sel, e);
      }
    }
  }
  // Drag source
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload("NYX_ENTITY", &e, sizeof(EntityID));
    ImGui::Text("Move: %s", nm.c_str());
    ImGui::EndDragDropSource();
  }
  char entCtx[64];
  std::snprintf(entCtx, sizeof(entCtx), "entity_ctx##%llu",
                (unsigned long long)treeId(e));
  if (ImGui::BeginPopupContextItem(entCtx)) {
    if (ImGui::MenuItem("Add Child Entity")) {
      EntityID child = world.createEntity("Entity");
      world.setParent(child, e);
    }
    if (ImGui::MenuItem("Add Submesh")) {
      auto &mc = world.ensureMesh(e);
      MeshSubmesh sm{};
      sm.name = "Submesh " + std::to_string(mc.submeshes.size());
      mc.submeshes.push_back(sm);
      world.events().push({WorldEventType::MeshChanged, e});
      engine.rebuildRenderables();
    }
    if (ImGui::MenuItem("Focus")) {
      sel.focusEntity = e;
    }
    if (ImGui::MenuItem("Rename")) {
      m_renameEntity = e;
      std::snprintf(m_renameEntityBuf.data(), m_renameEntityBuf.size(), "%s",
                    nm.c_str());
      char popupId[64];
      std::snprintf(popupId, sizeof(popupId), "rename_entity_popup##%llu",
                    (unsigned long long)treeId(e));
      ImGui::OpenPopup(popupId);
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
    if (m_copyEntity != InvalidEntity && ImGui::MenuItem("Paste (Sibling)")) {
      EntityID parent = world.parentOf(e);
      EntityID dup =
          world.duplicateSubtree(m_copyEntity, parent, &engine.materials());
      if (dup != InvalidEntity)
        sel.setSinglePick(packPick(dup, 0), dup);
    }
    if (m_copyEntity != InvalidEntity && ImGui::MenuItem("Paste (Child)")) {
      EntityID dup =
          world.duplicateSubtree(m_copyEntity, e, &engine.materials());
      if (dup != InvalidEntity)
        sel.setSinglePick(packPick(dup, 0), dup);
    }
    if (ImGui::MenuItem("Isolate")) {
      isolateEntity(world, e, m_editorCamera);
    }
    if (ImGui::MenuItem("Unisolate All")) {
      unisolateAll(world, m_editorCamera);
    }
    if (ImGui::MenuItem("Reset Transform")) {
      resetTransform(world, e);
    }
    if (ImGui::MenuItem("Reset Transform (Children)")) {
      resetTransformRecursive(world, e);
    }
    if (ImGui::MenuItem("Copy Transform")) {
      copyTransform(world, e);
    }
    if (ImGui::MenuItem("Paste Transform", nullptr, false,
                        m_hasCopiedTransform)) {
      pasteTransform(world, e);
    }
    if (ImGui::MenuItem("Delete (With Children)")) {
      world.destroyEntity(e);
      sel.removePicksForEntity(e);
      if (open && hasTreeContent)
        ImGui::TreePop();
      ImGui::EndPopup();
      return;
    }
    if (ImGui::MenuItem("Delete (Keep Children)")) {
      EntityID parent = world.parentOf(e);
      EntityID ch = world.hierarchy(e).firstChild;
      while (ch != InvalidEntity) {
        EntityID next = world.hierarchy(ch).nextSibling;
        world.setParentKeepWorld(ch, parent);
        ch = next;
      }
      world.destroyEntity(e);
      sel.removePicksForEntity(e);
      if (open && hasTreeContent)
        ImGui::TreePop();
      ImGui::EndPopup();
      return;
    }
    ImGui::EndPopup();
  }
  char popupId[64];
  std::snprintf(popupId, sizeof(popupId), "rename_entity_popup##%llu",
                (unsigned long long)treeId(e));
  if (ImGui::BeginPopup(popupId)) {
    if (m_renameEntity == e) {
      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::InputText("##RenameEntity", m_renameEntityBuf.data(),
                           m_renameEntityBuf.size(),
                           ImGuiInputTextFlags_EnterReturnsTrue) ||
          ImGui::IsItemDeactivatedAfterEdit()) {
        world.setName(e, m_renameEntityBuf.data());
        m_renameEntity = InvalidEntity;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }
  // Drop target => reparent
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload("NYX_ENTITY")) {
      EntityID dropped = *(const EntityID *)payload->Data;
      if (dropped != e)
        world.setParentKeepWorld(dropped, e);
    }
    ImGui::EndDragDropTarget();
  }
  // Material drop on entity row => apply to all submeshes
  {
    MaterialHandle dropped = InvalidMaterial;
    if (acceptMaterialDrop(dropped)) {
      applyMaterialToAllSubmeshes(world, e, dropped);
    }
  }
  // Show submeshes/materials only when open OR entity is selected
  const bool showMeshUI =
      hasSubmeshes && !world.hasLight(e) && (open || isSelected);
  if (showMeshUI) {
    auto &mc = world.mesh(e);
    ImGui::Indent();
    const uint32_t n = (uint32_t)mc.submeshes.size();
    for (uint32_t si = 0; si < n; ++si) {
      auto &sm = mc.submeshes[si];
      const uint32_t pid = packPick(e, si);
      const bool subSel = (sel.kind == SelectionKind::Picks && sel.hasPick(pid));
      ImGuiTreeNodeFlags sflags =
          ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
      if (subSel)
        sflags |= ImGuiTreeNodeFlags_Selected;
      const uintptr_t subId = treeId(e) ^ (uintptr_t(0xA1B20000u) + si);
      std::string subLabel = sm.name;
      if (subLabel.empty())
        subLabel = "Submesh " + std::to_string(si);
      const bool subOpen =
          ImGui::TreeNodeEx((void *)subId, sflags, "%s", subLabel.c_str());
      // Submesh click selection
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ImGuiIO &io = ImGui::GetIO();
        const bool ctrl = io.KeyCtrl;
        const bool shift = io.KeyShift;
        if (ctrl) {
          sel.togglePick(pid, e);
        } else if (shift) {
          sel.addPick(pid, e);
        } else {
          sel.setSinglePick(pid, e);
        }
        sel.activeEntity = e;
      }
      // Submesh context menu
      char subCtx[64];
      std::snprintf(subCtx, sizeof(subCtx), "submesh_ctx##%llu",
                    (unsigned long long)subId);
      if (ImGui::BeginPopupContextItem(subCtx)) {
        if (ImGui::MenuItem("Rename")) {
          m_renameEntity = e;
          std::snprintf(m_renameEntityBuf.data(), m_renameEntityBuf.size(), "%s",
                        subLabel.c_str());
          char popupId[64];
          std::snprintf(popupId, sizeof(popupId), "rename_submesh_popup##%llu",
                        (unsigned long long)subId);
          ImGui::OpenPopup(popupId);
        }
        if (ImGui::MenuItem("Duplicate")) {
          mc.submeshes.insert(mc.submeshes.begin() + (ptrdiff_t)si + 1, sm);
          world.events().push({WorldEventType::MeshChanged, e});
        }
        if (ImGui::MenuItem("Delete")) {
          mc.submeshes.erase(mc.submeshes.begin() + (ptrdiff_t)si);
          world.events().push({WorldEventType::MeshChanged, e});
          engine.rebuildRenderables();
          ImGui::EndPopup();
          if (subOpen)
            ImGui::TreePop();
          continue;
        }
        ImGui::EndPopup();
      }
      char subRenamePopup[64];
      std::snprintf(subRenamePopup, sizeof(subRenamePopup),
                    "rename_submesh_popup##%llu", (unsigned long long)subId);
      if (ImGui::BeginPopup(subRenamePopup)) {
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::InputText("##RenameSubmesh", m_renameEntityBuf.data(),
                             m_renameEntityBuf.size(),
                             ImGuiInputTextFlags_EnterReturnsTrue) ||
            ImGui::IsItemDeactivatedAfterEdit()) {
          sm.name = m_renameEntityBuf.data();
          world.events().push({WorldEventType::MeshChanged, e});
          m_renameEntity = InvalidEntity;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
      // Material drop on submesh row => apply to this submesh
      {
        MaterialHandle dropped = InvalidMaterial;
        if (acceptMaterialDrop(dropped)) {
          applyMaterialToSubmesh(world, e, si, dropped);
        }
      }
      // Material node (uses SAME pickID; Inspector decides to show material UI)
      const bool showMat = subOpen || subSel;
      if (showMat) {
        ImGui::Indent();
        ImGuiTreeNodeFlags mflags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                    ImGuiTreeNodeFlags_Leaf |
                                    ImGuiTreeNodeFlags_NoTreePushOnOpen;
        // "selected" if this submesh pick is active (nice UX)
        if (sel.kind == SelectionKind::Picks && sel.activePick == pid)
          mflags |= ImGuiTreeNodeFlags_Selected;
        const uintptr_t matId = treeId(e) ^ (uintptr_t(0x9E370000u) + si);
        std::string matLabel = "Material";
        if (sm.material != InvalidMaterial && engine.materials().isAlive(sm.material)) {
          const std::string &name = engine.materials().cpu(sm.material).name;
          if (!name.empty())
            matLabel = name;
        }
        const float frameH2 = ImGui::GetFrameHeight();
        const float thumb = std::min(18.0f, std::max(12.0f, frameH2 - 2.0f));
        if (sm.material != InvalidMaterial && engine.materials().isAlive(sm.material)) {
          MatThumb &mth = getMaterialThumb(engine, sm.material);
          if (mth.ready && mth.tex != 0) {
            ImGui::Image((ImTextureID)(uintptr_t)mth.tex,
                         ImVec2(thumb, thumb), ImVec2(0, 1), ImVec2(1, 0));
          } else {
            ImGui::Dummy(ImVec2(thumb, thumb));
          }
        } else {
          ImGui::Dummy(ImVec2(thumb, thumb));
        }
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TreeNodeEx((void *)matId, mflags, "%s", matLabel.c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
          // set active pick to this submesh; keep multi-selection if ctrl/shift
          ImGuiIO &io = ImGui::GetIO();
          if (io.KeyCtrl) {
            sel.togglePick(pid, e);
          } else if (io.KeyShift) {
            sel.addPick(pid, e);
          } else {
            sel.setSinglePick(pid, e);
          }
          sel.activeEntity = e;
        }
        // Drag material
        {
          MaterialHandle mh = sm.material;
          beginMaterialDragSource(mh, matLabel.c_str());
        }
        // Drop material onto material row
        {
          MaterialHandle dropped = InvalidMaterial;
          if (acceptMaterialDrop(dropped)) {
            applyMaterialToSubmesh(world, e, si, dropped);
          }
        }
        // Context menu: Copy/Paste
        char matCtx[64];
        std::snprintf(matCtx, sizeof(matCtx), "mat_ctx##%llu",
                      (unsigned long long)matId);
        if (ImGui::BeginPopupContextItem(matCtx)) {
          if (ImGui::MenuItem("Copy")) {
            m_matClipboard = sm.material;
          }
          const bool canPaste = (m_matClipboard != InvalidMaterial);
          if (ImGui::MenuItem("Paste", nullptr, false, canPaste)) {
            applyMaterialToSubmesh(world, e, si, m_matClipboard);
          }
          ImGui::EndPopup();
        }
        ImGui::Unindent();
      }
      if (subOpen)
        ImGui::TreePop();
    }
    ImGui::Unindent();
  }
  // Children
  if (open && hasTreeContent) {
    if (hasChildren) {
      EntityID child = world.hierarchy(e).firstChild;
      while (child != InvalidEntity) {
        EntityID next = world.hierarchy(child).nextSibling;
        drawEntityNode(world, child, engine, sel);
        child = next;
      }
    }
    ImGui::TreePop();
  }
}

} // namespace Nyx
