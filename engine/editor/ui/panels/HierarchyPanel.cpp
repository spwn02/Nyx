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

// Material drag/drop + clipboard
static MaterialHandle g_matClipboard = InvalidMaterial;

static bool beginMaterialDragSource(MaterialHandle mh, const char *label) {
  if (mh == InvalidMaterial)
    return false;
  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
    UiPayload::MaterialHandlePayload payload{mh.slot, mh.gen};
    ImGui::SetDragDropPayload(UiPayload::MaterialHandle, &payload,
                              sizeof(payload));
    ImGui::Text("Material: %s", label ? label : "(unnamed)");
    ImGui::EndDragDropSource();
    return true;
  }
  return false;
}

static bool acceptMaterialDrop(MaterialHandle &outMh) {
  if (!ImGui::BeginDragDropTarget())
    return false;
  if (const ImGuiPayload *p =
          ImGui::AcceptDragDropPayload(UiPayload::MaterialHandle)) {
    if (p->Data && p->DataSize == sizeof(UiPayload::MaterialHandlePayload)) {
      const auto *pl = (const UiPayload::MaterialHandlePayload *)p->Data;
      outMh = MaterialHandle{pl->slot, pl->gen};
      ImGui::EndDragDropTarget();
      return true;
    }
  }
  ImGui::EndDragDropTarget();
  return false;
}

static void applyMaterialToSubmesh(World &world, EntityID e, uint32_t si,
                                   MaterialHandle mh) {
  if (!world.hasMesh(e) || world.hasLight(e))
    return;
  world.submesh(e, si).material = mh;
  world.events().push({WorldEventType::MeshChanged, e});
}

static void applyMaterialToAllSubmeshes(World &world, EntityID e,
                                        MaterialHandle mh) {
  if (!world.hasMesh(e) || world.hasLight(e))
    return;
  const uint32_t n = world.submeshCount(e);
  for (uint32_t si = 0; si < n; ++si) {
    applyMaterialToSubmesh(world, e, si, mh);
  }
  world.events().push({WorldEventType::MeshChanged, e});
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

struct MatThumb {
  uint32_t tex = 0;
  bool ready = false;
  bool pending = false;
  bool saved = false;
  std::string cachePath;
};

static std::unordered_map<uint64_t, MatThumb> g_matThumbs;
static uint64_t g_matThumbSettingsHash = 0;
static EntityID g_renameEntity = InvalidEntity;
static char g_renameEntityBuf[128]{};
static EntityID g_copyEntity = InvalidEntity;
static bool g_hasCopiedTransform = false;
static glm::vec3 g_copyTranslation{0.0f};
static glm::quat g_copyRotation{1.0f, 0.0f, 0.0f, 0.0f};
static glm::vec3 g_copyScale{1.0f};

static uint64_t hashPreviewSettings(const EngineContext &engine) {
  const glm::vec3 &d = engine.previewLightDir();
  const glm::vec3 &c = engine.previewLightColor();
  const float i = engine.previewLightIntensity();
  const float e = engine.previewLightExposure();
  const float a = engine.previewAmbient();
  auto hf = [](float v) -> uint64_t {
    uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(u));
    return (uint64_t)u;
  };
  uint64_t h = 1469598103934665603ull;
  h ^= hf(d.x) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(d.y) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(d.z) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(c.x) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(c.y) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(c.z) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(i) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(e) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(a) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  return h;
}

static uint64_t hashMaterialData(const MaterialData &m) {
  auto hmix = [](uint64_t h, uint64_t v) -> uint64_t {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
  };
  auto hf = [](float v) -> uint64_t {
    uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(u));
    return (uint64_t)u;
  };
  uint64_t h = 1469598103934665603ull;
  h = hmix(h, hf(m.baseColorFactor.x));
  h = hmix(h, hf(m.baseColorFactor.y));
  h = hmix(h, hf(m.baseColorFactor.z));
  h = hmix(h, hf(m.baseColorFactor.w));
  h = hmix(h, hf(m.emissiveFactor.x));
  h = hmix(h, hf(m.emissiveFactor.y));
  h = hmix(h, hf(m.emissiveFactor.z));
  h = hmix(h, hf(m.metallic));
  h = hmix(h, hf(m.roughness));
  h = hmix(h, hf(m.ao));
  h = hmix(h, hf(m.uvScale.x));
  h = hmix(h, hf(m.uvScale.y));
  h = hmix(h, hf(m.uvOffset.x));
  h = hmix(h, hf(m.uvOffset.y));
  h = hmix(h, (uint64_t)m.alphaMode);
  h = hmix(h, hf(m.alphaCutoff));
  h = hmix(h, m.tangentSpaceNormal ? 0xA5A5A5A5u : 0x5A5A5A5Au);
  for (const auto &p : m.texPath) {
    for (char c : p)
      h = hmix(h, (uint64_t)(uint8_t)c);
  }
  for (char c : m.name)
    h = hmix(h, (uint64_t)(uint8_t)c);
  return h;
}

static std::filesystem::path matPreviewCacheDir() {
  static bool s_init = false;
  static std::filesystem::path s_dir;
  if (!s_init) {
    s_init = true;
    s_dir = std::filesystem::current_path() / ".nyx" / "matpreviewcache";
    std::error_code ec;
    std::filesystem::create_directories(s_dir, ec);
  }
  return s_dir;
}

static std::string matPreviewCachePath(MaterialHandle h,
                                       const MaterialData &md,
                                       uint64_t settingsHash) {
  const uint64_t key = (uint64_t(h.slot) << 32) | uint64_t(h.gen);
  const uint64_t dataHash = hashMaterialData(md);
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%016llx_%016llx_%016llx.png",
                (unsigned long long)key, (unsigned long long)dataHash,
                (unsigned long long)settingsHash);
  return (matPreviewCacheDir() / buf).string();
}

static MatThumb &getMaterialThumb(EngineContext &engine, MaterialHandle h) {
  const uint64_t key = (uint64_t(h.slot) << 32) | uint64_t(h.gen);
  const uint32_t lastCaptured = engine.lastPreviewCaptureTex();
  MatThumb &th = g_matThumbs[key];
  if (th.tex == 0) {
    glCreateTextures(GL_TEXTURE_2D, 1, &th.tex);
    glTextureParameteri(th.tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(th.tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(th.tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(th.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureStorage2D(th.tex, 1, GL_RGBA8, 64, 64);
    const uint32_t zero = 0;
    glClearTexImage(th.tex, 0, GL_RGBA, GL_UNSIGNED_BYTE, &zero);
    th.ready = false;
    th.pending = false;
    th.saved = false;
  }
  if (engine.materials().isAlive(h)) {
    const MaterialData &md = engine.materials().cpu(h);
    th.cachePath = matPreviewCachePath(h, md, g_matThumbSettingsHash);
  } else {
    th.cachePath.clear();
  }
  if (!th.ready && !th.pending && !th.cachePath.empty()) {
    if (std::filesystem::exists(th.cachePath)) {
      int w = 0, hgt = 0, comp = 0;
      unsigned char *data =
          stbi_load(th.cachePath.c_str(), &w, &hgt, &comp, 4);
      if (data && w > 0 && hgt > 0) {
        glTextureSubImage2D(th.tex, 0, 0, 0, w, hgt, GL_RGBA,
                            GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
        th.ready = true;
        th.saved = true;
      } else if (data) {
        stbi_image_free(data);
      }
    }
  }
  if (lastCaptured != 0 && lastCaptured == th.tex) {
    th.ready = true;
    th.pending = false;
  }
  if (!th.ready && !th.pending) {
    engine.requestMaterialPreview(h, th.tex);
    th.pending = true;
  }
  if (th.ready && !th.saved && !th.cachePath.empty()) {
    const int w = 64;
    const int hgt = 64;
    std::vector<uint8_t> rgba((size_t)w * (size_t)hgt * 4u);
    glGetTextureImage(th.tex, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                      (GLsizei)rgba.size(), rgba.data());
    stbi_write_png(th.cachePath.c_str(), w, hgt, 4, rgba.data(), w * 4);
    th.saved = true;
  }
  return th;
}

static void clearMaterialFromWorld(World &world, MaterialHandle h) {
  if (h == InvalidMaterial)
    return;
  for (EntityID e : world.alive()) {
    if (!world.isAlive(e) || !world.hasMesh(e))
      continue;
    const uint32_t n = world.submeshCount(e);
    for (uint32_t si = 0; si < n; ++si) {
      auto &sm = world.submesh(e, si);
      if (sm.material == h)
        sm.material = InvalidMaterial;
    }
  }
}

static void setHiddenRecursive(World &world, EntityID e, bool hidden) {
  if (!world.isAlive(e))
    return;
  auto &tr = world.transform(e);
  tr.hidden = hidden;
  EntityID ch = world.hierarchy(e).firstChild;
  while (ch != InvalidEntity) {
    EntityID next = world.hierarchy(ch).nextSibling;
    setHiddenRecursive(world, ch, hidden);
    ch = next;
  }
}

static void isolateEntity(World &world, EntityID e, EntityID keepVisible) {
  for (EntityID id : world.alive()) {
    if (!world.isAlive(id))
      continue;
    world.transform(id).hidden = true;
  }
  if (keepVisible != InvalidEntity && world.isAlive(keepVisible))
    world.transform(keepVisible).hidden = false;
  setHiddenRecursive(world, e, false);
}

static void unisolateAll(World &world, EntityID keepVisible) {
  for (EntityID id : world.alive()) {
    if (!world.isAlive(id))
      continue;
    world.transform(id).hidden = false;
  }
  if (keepVisible != InvalidEntity && world.isAlive(keepVisible))
    world.transform(keepVisible).hidden = false;
}

static void resetTransform(World &world, EntityID e) {
  if (!world.isAlive(e))
    return;
  auto &t = world.transform(e);
  t.translation = glm::vec3(0.0f);
  t.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  t.scale = glm::vec3(1.0f);
  t.dirty = true;
  world.worldTransform(e).dirty = true;
}

static void copyTransform(World &world, EntityID e) {
  if (!world.isAlive(e))
    return;
  const auto &t = world.transform(e);
  g_copyTranslation = t.translation;
  g_copyRotation = t.rotation;
  g_copyScale = t.scale;
  g_hasCopiedTransform = true;
}

static void pasteTransform(World &world, EntityID e) {
  if (!world.isAlive(e) || !g_hasCopiedTransform)
    return;
  auto &t = world.transform(e);
  t.translation = g_copyTranslation;
  t.rotation = g_copyRotation;
  t.scale = g_copyScale;
  t.dirty = true;
  world.worldTransform(e).dirty = true;
}

static void resetTransformRecursive(World &world, EntityID e) {
  resetTransform(world, e);
  EntityID ch = world.hierarchy(e).firstChild;
  while (ch != InvalidEntity) {
    EntityID next = world.hierarchy(ch).nextSibling;
    resetTransformRecursive(world, ch);
    ch = next;
  }
}

static void selectEntities(World &world, Selection &sel,
                           const std::vector<EntityID> &ents) {
  sel.kind = SelectionKind::Picks;
  sel.picks.clear();
  sel.pickEntity.clear();
  for (EntityID e : ents) {
    if (!world.isAlive(e))
      continue;
    const uint32_t pid = packPick(e, 0);
    sel.picks.push_back(pid);
    sel.pickEntity.emplace(pid, e);
  }
  if (!sel.picks.empty()) {
    sel.activePick = sel.picks.back();
    sel.activeEntity = sel.entityForPick(sel.activePick);
  } else {
    sel.clear();
  }
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
                                const std::vector<EntityID> &order, EntityID a,
                                EntityID b) {
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

void HierarchyPanel::rebuildRoots(World &world) { m_roots = world.roots(); }

void HierarchyPanel::addRoot(EntityID e) {
  if (e == InvalidEntity)
    return;
  if (std::find(m_roots.begin(), m_roots.end(), e) != m_roots.end())
    return;
  auto it = std::lower_bound(m_roots.begin(), m_roots.end(), e,
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
  if (curPreviewHash != g_matThumbSettingsHash) {
    for (auto &kv : g_matThumbs) {
      kv.second.ready = false;
      kv.second.pending = false;
      kv.second.saved = false;
    }
    g_matThumbSettingsHash = curPreviewHash;
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
    if (g_copyEntity != InvalidEntity &&
        ImGui::MenuItem("Paste (Root)")) {
      EntityID dup =
          world.duplicateSubtree(g_copyEntity, InvalidEntity, &engine.materials());
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
    for (uint32_t p : sel.picks) {
      EntityID e = sel.entityForPick(p);
      if (e == InvalidEntity)
        e = engine.resolveEntityIndex(pickEntity(p));
      if (e == InvalidEntity || !world.isAlive(e))
        continue;
      if (std::find(ents.begin(), ents.end(), e) == ents.end())
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
          if (g_renameEntity == e) {
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("##RenameEnt", g_renameEntityBuf,
                                 sizeof(g_renameEntityBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue) ||
                ImGui::IsItemDeactivatedAfterEdit()) {
              world.setName(e, g_renameEntityBuf);
              g_renameEntity = InvalidEntity;
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
            g_renameEntity = e;
            std::snprintf(g_renameEntityBuf, sizeof(g_renameEntityBuf), "%s",
                          nm.c_str());
          }

      char catEntCtx[64];
      std::snprintf(catEntCtx, sizeof(catEntCtx), "cat_ent_ctx##%llu",
                    (unsigned long long)treeId(e));
      if (ImGui::BeginPopupContextItem(catEntCtx)) {
        if (ImGui::MenuItem("Rename")) {
          g_renameEntity = e;
          std::snprintf(g_renameEntityBuf, sizeof(g_renameEntityBuf), "%s",
                        nm.c_str());
        }
        if (ImGui::MenuItem("Focus")) {
          sel.focusEntity = e;
        }
        if (ImGui::MenuItem("Copy")) {
          g_copyEntity = e;
        }
        if (ImGui::MenuItem("Duplicate")) {
          EntityID parent = world.parentOf(e);
          EntityID dup = world.duplicateSubtree(e, parent, &engine.materials());
          if (dup != InvalidEntity) {
            sel.setSinglePick(packPick(dup, 0), dup);
          }
        }
        if (g_copyEntity != InvalidEntity &&
            ImGui::MenuItem("Paste (Sibling)")) {
          EntityID parent = world.parentOf(e);
          EntityID dup =
              world.duplicateSubtree(g_copyEntity, parent, &engine.materials());
          if (dup != InvalidEntity)
            sel.setSinglePick(packPick(dup, 0), dup);
        }
        if (g_copyEntity != InvalidEntity &&
            ImGui::MenuItem("Paste (Child)")) {
          EntityID dup =
              world.duplicateSubtree(g_copyEntity, e, &engine.materials());
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
                            g_hasCopiedTransform)) {
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
      g_renameEntity = e;
      std::snprintf(g_renameEntityBuf, sizeof(g_renameEntityBuf), "%s",
                    nm.c_str());
      char popupId[64];
      std::snprintf(popupId, sizeof(popupId), "rename_entity_popup##%llu",
                    (unsigned long long)treeId(e));
      ImGui::OpenPopup(popupId);
    }
    if (ImGui::MenuItem("Copy")) {
      g_copyEntity = e;
    }
    if (ImGui::MenuItem("Duplicate")) {
      EntityID parent = world.parentOf(e);
      EntityID dup = world.duplicateSubtree(e, parent, &engine.materials());
      if (dup != InvalidEntity) {
        sel.setSinglePick(packPick(dup, 0), dup);
      }
    }
    if (g_copyEntity != InvalidEntity &&
        ImGui::MenuItem("Paste (Sibling)")) {
      EntityID parent = world.parentOf(e);
      EntityID dup =
          world.duplicateSubtree(g_copyEntity, parent, &engine.materials());
      if (dup != InvalidEntity)
        sel.setSinglePick(packPick(dup, 0), dup);
    }
    if (g_copyEntity != InvalidEntity &&
        ImGui::MenuItem("Paste (Child)")) {
      EntityID dup =
          world.duplicateSubtree(g_copyEntity, e, &engine.materials());
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
                        g_hasCopiedTransform)) {
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
    if (g_renameEntity == e) {
      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::InputText("##RenameEntity", g_renameEntityBuf,
                           sizeof(g_renameEntityBuf),
                           ImGuiInputTextFlags_EnterReturnsTrue) ||
          ImGui::IsItemDeactivatedAfterEdit()) {
        world.setName(e, g_renameEntityBuf);
        g_renameEntity = InvalidEntity;
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

      const bool subSel =
          (sel.kind == SelectionKind::Picks && sel.hasPick(pid));
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
          g_renameEntity = e;
          std::snprintf(g_renameEntityBuf, sizeof(g_renameEntityBuf), "%s",
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
                    "rename_submesh_popup##%llu",
                    (unsigned long long)subId);
      if (ImGui::BeginPopup(subRenamePopup)) {
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::InputText("##RenameSubmesh", g_renameEntityBuf,
                             sizeof(g_renameEntityBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue) ||
            ImGui::IsItemDeactivatedAfterEdit()) {
          sm.name = g_renameEntityBuf;
          world.events().push({WorldEventType::MeshChanged, e});
          g_renameEntity = InvalidEntity;
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

        const float frameH = ImGui::GetFrameHeight();
        const float thumb = std::min(18.0f, std::max(12.0f, frameH - 2.0f));
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
            g_matClipboard = sm.material;
          }
          const bool canPaste = (g_matClipboard != InvalidMaterial);
          if (ImGui::MenuItem("Paste", nullptr, false, canPaste)) {
            applyMaterialToSubmesh(world, e, si, g_matClipboard);
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
