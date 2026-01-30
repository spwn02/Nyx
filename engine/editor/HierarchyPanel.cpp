#include "HierarchyPanel.h"

#include "editor/Selection.h"
#include "core/Paths.h"
#include "scene/Pick.h"
#include "scene/SelectionCycler.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <vector>

namespace Nyx {

static const char *meshTypeName(ProcMeshType t) {
  switch (t) {
  case ProcMeshType::Cube:
    return "Cube";
  case ProcMeshType::Plane:
    return "Plane";
  case ProcMeshType::Circle:
    return "Circle";
  case ProcMeshType::Sphere:
    return "Sphere";
  case ProcMeshType::Monkey:
    return "Monkey";
  default:
    return "Unknown";
  }
}

static uintptr_t treeId(EntityID e) {
  return (uintptr_t(e.generation) << 32) | uintptr_t(e.index);
}

static void DrawAtlasIconAt(const Nyx::IconAtlas &atlas,
                            const Nyx::AtlasRegion &r, ImVec2 p, ImVec2 size,
                            ImU32 tint = IM_COL32(220, 220, 220, 255)) {
  p.x = std::floor(p.x + 0.5f);
  p.y = std::floor(p.y + 0.5f);
  size.x = std::floor(size.x + 0.5f);
  size.y = std::floor(size.y + 0.5f);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddImage(atlas.imguiTexId(), p, ImVec2(p.x + size.x, p.y + size.y), r.uv0,
               r.uv1, tint);
}

static void gatherEntityPicks(World &world, EntityID e,
                              std::vector<uint32_t> &out) {
  if (!world.isAlive(e))
    return;

  if (!world.hasMesh(e)) {
    // still allow selecting entity even without mesh: represent it as submesh 0
    // (you can also choose to skip this entirely)
    out.push_back(packPick(e, 0));
    return;
  }

  const uint32_t n = world.submeshCount(e);
  if (n == 0) {
    out.push_back(packPick(e, 0));
    return;
  }

  out.reserve(out.size() + n);
  for (uint32_t si = 0; si < n; ++si)
    out.push_back(packPick(e, si));
}

static void setSingleEntity(World &world, Selection &sel, EntityID e) {
  std::vector<uint32_t> tmp;
  gatherEntityPicks(world, e, tmp);
  if (tmp.empty()) {
    sel.clear();
    return;
  }
  sel.kind = SelectionKind::Picks;
  sel.picks = tmp;
  sel.activePick = tmp.front();
  sel.activeEntity = e;
  sel.pickEntity.clear();
  for (uint32_t p : tmp)
    sel.pickEntity.emplace(p, e);
}

static void addEntity(World &world, Selection &sel, EntityID e) {
  std::vector<uint32_t> tmp;
  gatherEntityPicks(world, e, tmp);
  if (tmp.empty())
    return;

  if (sel.kind != SelectionKind::Picks) {
    sel.kind = SelectionKind::Picks;
    sel.picks.clear();
    sel.pickEntity.clear();
  }

  for (uint32_t p : tmp) {
    if (!sel.hasPick(p))
      sel.picks.push_back(p);
  }
  sel.activePick = tmp.front();
  sel.activeEntity = e;
  for (uint32_t p : tmp)
    sel.pickEntity.emplace(p, e);
}

static void toggleEntity(World &world, Selection &sel, EntityID e) {
  std::vector<uint32_t> tmp;
  gatherEntityPicks(world, e, tmp);
  if (tmp.empty())
    return;

  if (sel.kind != SelectionKind::Picks) {
    // toggle on => single-entity
    setSingleEntity(world, sel, e);
    return;
  }

  // If ALL picks are present => remove them. Else => add missing.
  bool allPresent = true;
  for (uint32_t p : tmp) {
    if (!sel.hasPick(p)) {
      allPresent = false;
      break;
    }
  }

  if (allPresent) {
    // remove all of tmp from sel.picks
    auto &v = sel.picks;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](uint32_t x) {
                             for (uint32_t p : tmp)
                               if (p == x)
                                 return true;
                             return false;
                           }),
            v.end());
    for (uint32_t p : tmp)
      sel.pickEntity.erase(p);

    if (v.empty()) {
      sel.clear();
    } else {
      sel.activePick = v.back();
      sel.activeEntity = sel.entityForPick(sel.activePick);
    }
  } else {
    for (uint32_t p : tmp) {
      if (!sel.hasPick(p))
        sel.picks.push_back(p);
    }
    sel.activePick = tmp.front();
    sel.activeEntity = e;
    for (uint32_t p : tmp)
      sel.pickEntity.emplace(p, e);
  }
}

static void rangeSelectEntities(World &world, Selection &sel,
                                const std::vector<EntityID> &order,
                                EntityID a, EntityID b) {
  if (a == InvalidEntity || b == InvalidEntity) {
    setSingleEntity(world, sel, b);
    return;
  }

  auto ia = std::find(order.begin(), order.end(), a);
  auto ib = std::find(order.begin(), order.end(), b);
  if (ia == order.end() || ib == order.end()) {
    setSingleEntity(world, sel, b);
    return;
  }
  if (ia > ib)
    std::swap(ia, ib);

  sel.kind = SelectionKind::Picks;
  sel.picks.clear();
  sel.pickEntity.clear();

  for (auto it = ia; it != std::next(ib); ++it) {
    std::vector<uint32_t> tmp;
    gatherEntityPicks(world, *it, tmp);
    for (uint32_t p : tmp) {
      sel.picks.push_back(p);
      sel.pickEntity.emplace(p, *it);
    }
  }

  if (!sel.picks.empty()) {
    sel.activePick = packPick(b, 0);
    sel.activeEntity = b;
    sel.pickEntity.emplace(sel.activePick, b);
  } else {
    sel.clear();
  }
}

static bool isEntityHighlightedByPicks(const Selection &sel, EntityID e,
                                       uint32_t subCount) {
  if (sel.kind != SelectionKind::Picks || sel.picks.empty())
    return false;

  for (uint32_t si = 0; si < std::max(1u, subCount); ++si) {
    const uint32_t p = packPick(e, si);
    if (sel.hasPick(p))
      return true;
  }
  return false;
}

void HierarchyPanel::setWorld(World *world) {
  m_roots.clear();
  m_visibleOrder.clear();
  if (world)
    rebuildRoots(*world);
}

void HierarchyPanel::rebuildRoots(World &world) {
  m_roots = world.roots();
}

void HierarchyPanel::addRoot(EntityID e) {
  if (e == InvalidEntity)
    return;
  if (std::find(m_roots.begin(), m_roots.end(), e) != m_roots.end())
    return;
  auto it = std::lower_bound(
      m_roots.begin(), m_roots.end(), e,
      [](EntityID a, EntityID b) {
        if (a.index != b.index)
          return a.index < b.index;
        return a.generation < b.generation;
      });
  m_roots.insert(it, e);
}

void HierarchyPanel::removeRoot(EntityID e) {
  m_roots.erase(std::remove(m_roots.begin(), m_roots.end(), e), m_roots.end());
}

void HierarchyPanel::onWorldEvent(World &world, const WorldEvent &e) {
  switch (e.type) {
  case WorldEventType::EntityCreated:
    if (world.isAlive(e.a) && world.parentOf(e.a) == InvalidEntity)
      addRoot(e.a);
    break;
  case WorldEventType::EntityDestroyed:
    removeRoot(e.a);
    break;
  case WorldEventType::ParentChanged:
    if (e.b == InvalidEntity)
      addRoot(e.a);
    else
      removeRoot(e.a);
    break;
  default:
    break;
  }
}

void HierarchyPanel::draw(World &world, Selection &sel) {
  if (!m_iconInit) {
    m_iconInit = true;
    const std::filesystem::path iconDir = Paths::engineRes() / "icons";
    const std::filesystem::path jsonPath = Paths::engineRes() / "icon_atlas.json";
    const std::filesystem::path pngPath = Paths::engineRes() / "icon_atlas.png";

    if (std::filesystem::exists(jsonPath) && std::filesystem::exists(pngPath)) {
      m_iconReady = m_iconAtlas.loadFromJson(jsonPath.string());
    } else {
      m_iconReady = m_iconAtlas.buildFromFolder(
          iconDir.string(), jsonPath.string(), pngPath.string(), 64, 0);
    }
  }

  ImGui::Begin("Hierarchy");

  m_visibleOrder.clear();

  // Click empty space to deselect
  if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() &&
      !ImGui::IsAnyItemHovered()) {
    sel.clear();
  }

  // Drop onto empty window space => make root
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload("NYX_ENTITY")) {
      EntityID dropped = *(const EntityID *)payload->Data;
      world.setParentKeepWorld(dropped, InvalidEntity);
    }
    ImGui::EndDragDropTarget();
  }

  for (EntityID e : m_roots)
    drawEntityNode(world, e, sel);

  ImGui::Dummy(ImVec2(0.0f, 200.0f));
  ImGui::End();
}

void HierarchyPanel::drawEntityNode(World &world, EntityID e, Selection &sel) {
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
    std::snprintf(label, sizeof(label), "%s  [%s]", nm.c_str(), meshTypeName(t));
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

  // Show submeshes/materials only when open OR entity is selected
  const bool showMeshUI = hasSubmeshes && (open || isSelected);
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
      const bool subOpen =
          ImGui::TreeNodeEx((void *)subId, sflags, "%s", sm.name.c_str());

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

      // Material node (uses SAME pickID; Inspector decides to show material UI)
      const bool showMat = subOpen || subSel;
      if (showMat) {
        ImGui::Indent();

        ImGuiTreeNodeFlags mflags =
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf |
            ImGuiTreeNodeFlags_NoTreePushOnOpen;

        // "selected" if this submesh pick is active (nice UX)
        if (sel.kind == SelectionKind::Picks && sel.activePick == pid)
          mflags |= ImGuiTreeNodeFlags_Selected;

        const uintptr_t matId = treeId(e) ^ (uintptr_t(0x9E370000u) + si);
        ImGui::TreeNodeEx((void *)matId, mflags, "Material");

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
        drawEntityNode(world, child, sel);
        child = next;
      }
    }
    ImGui::TreePop();
  }
}

} // namespace Nyx
