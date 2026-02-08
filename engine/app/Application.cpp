#include "Application.h"

#include "AppContext.h"
#include "EngineContext.h"

#include "core/Assert.h"
#include "core/Log.h"

#include "editor/EditorLayer.h"
#include "editor/Selection.h"
#include "editor/tools/DockspaceLayout.h"
#include "editor/tools/EditorStateIO.h"
#include "editor/tools/ProjectSerializer.h"
#include "editor/tools/ViewportPick.h"

#include "input/KeyCodes.h"
#include "input/Keybinds.h"
#include "platform/GLFWWindow.h"

#include "input/InputSystem.h"
#include "scene/EntityID.h"
#include "scene/Pick.h"
#include "scene/World.h"
#include "scene/WorldSerializer.h"

#include <filesystem>
#include <glm/gtx/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <vector>

#include <ImGuizmo.h>

namespace Nyx {

Application::Application(std::unique_ptr<AppContext> app,
                         std::unique_ptr<EngineContext> engine)
    : m_app(std::move(app)), m_engine(std::move(engine)) {
  NYX_ASSERT(m_app != nullptr, "Application requires AppContext");
  NYX_ASSERT(m_engine != nullptr, "Application requires EngineContext");
  setupKeybinds();
}

Application::~Application() = default;

static bool isShiftDown(const InputSystem &in) {
  return in.isDown(Key::LeftShift) || in.isDown(Key::RightShift);
}
static bool isCtrlDown(const InputSystem &in) {
  return in.isDown(Key::LeftCtrl) || in.isDown(Key::RightCtrl);
}

static void deleteSelection(World &world, Selection &sel);
static void duplicateSelection(EngineContext &engine, Selection &sel);

void Application::setupKeybinds() {
  m_keybinds.clear();

  auto canUseEditorShortcuts = [this]() -> bool {
    if (!m_app->isEditorVisible() || !m_app->editorLayer())
      return false;
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantTextInput)
      return false;
    if (m_engine->uiBlockGlobalShortcuts())
      return false;
    return true;
  };

  Keybind save{};
  save.id = "save_scene";
  save.chord.keys = {Key::S};
  save.chord.mods = KeyMod::Ctrl;
  save.chord.allowExtraKeys = false;
  save.priority = 10;
  save.enabled = canUseEditorShortcuts;
  save.action = [this]() {
    if (auto *ed = m_app->editorLayer())
      ed->requestSaveScene(*m_engine);
  };
  m_keybinds.add(std::move(save));

  Keybind saveAs{};
  saveAs.id = "save_scene_as";
  saveAs.chord.keys = {Key::S};
  saveAs.chord.mods = KeyMod::Ctrl | KeyMod::Shift;
  saveAs.chord.allowExtraKeys = false;
  saveAs.priority = 20;
  saveAs.enabled = canUseEditorShortcuts;
  saveAs.action = [this]() {
    if (auto *ed = m_app->editorLayer())
      ed->requestSaveSceneAs();
  };
  m_keybinds.add(std::move(saveAs));

  Keybind quit{};
  quit.id = "quit";
  quit.chord.keys = {Key::Q};
  quit.chord.mods = KeyMod::Ctrl;
  quit.chord.allowExtraKeys = false;
  quit.priority = 5;
  quit.enabled = canUseEditorShortcuts;
  quit.action = [this]() { m_app->window().requestClose(); };
  m_keybinds.add(std::move(quit));

  Keybind undo{};
  undo.id = "undo";
  undo.chord.keys = {Key::Z};
  undo.chord.mods = KeyMod::Ctrl;
  undo.chord.allowExtraKeys = false;
  undo.priority = 15;
  undo.enabled = canUseEditorShortcuts;
  undo.action = [this]() {
    if (auto *ed = m_app->editorLayer())
      ed->undo(*m_engine);
  };
  m_keybinds.add(std::move(undo));

  Keybind redo{};
  redo.id = "redo";
  redo.chord.keys = {Key::Z};
  redo.chord.mods = KeyMod::Ctrl | KeyMod::Shift;
  redo.chord.allowExtraKeys = false;
  redo.priority = 25;
  redo.enabled = canUseEditorShortcuts;
  redo.action = [this]() {
    if (auto *ed = m_app->editorLayer())
      ed->redo(*m_engine);
  };
  m_keybinds.add(std::move(redo));

  Keybind duplicate{};
  duplicate.id = "duplicate_selection";
  duplicate.chord.keys = {Key::D};
  duplicate.chord.mods = KeyMod::Shift;
  duplicate.chord.allowExtraKeys = true;
  duplicate.priority = 5;
  duplicate.enabled = canUseEditorShortcuts;
  duplicate.action = [this]() {
    if (auto *ed = m_app->editorLayer())
      duplicateSelection(*m_engine, ed->selection());
  };
  m_keybinds.add(std::move(duplicate));

  Keybind deleteKey{};
  deleteKey.id = "delete_selection";
  deleteKey.chord.keys = {Key::Delete};
  deleteKey.chord.allowExtraKeys = true;
  deleteKey.priority = 5;
  deleteKey.enabled = canUseEditorShortcuts;
  deleteKey.action = [this]() {
    if (auto *ed = m_app->editorLayer())
      deleteSelection(m_engine->world(), ed->selection());
  };
  m_keybinds.add(std::move(deleteKey));

  Keybind deleteX{};
  deleteX.id = "delete_selection_x";
  deleteX.chord.keys = {Key::X};
  deleteX.chord.allowExtraKeys = true;
  deleteX.priority = 5;
  deleteX.enabled = canUseEditorShortcuts;
  deleteX.action = [this]() {
    if (auto *ed = m_app->editorLayer())
      deleteSelection(m_engine->world(), ed->selection());
  };
  m_keybinds.add(std::move(deleteX));

  auto canUseGizmoShortcuts = [this, canUseEditorShortcuts]() -> bool {
    if (!canUseEditorShortcuts())
      return false;
    if (auto *ed = m_app->editorLayer()) {
      if (ed->cameraController().mouseCaptured)
        return false;
    }
    return true;
  };

  Keybind gizmoTranslate{};
  gizmoTranslate.id = "gizmo_translate";
  gizmoTranslate.chord.keys = {Key::W};
  gizmoTranslate.chord.allowExtraKeys = true;
  gizmoTranslate.priority = 3;
  gizmoTranslate.enabled = canUseGizmoShortcuts;
  gizmoTranslate.action = [this]() {
    if (auto *ed = m_app->editorLayer())
      ed->gizmo().op = GizmoOp::Translate;
  };
  m_keybinds.add(std::move(gizmoTranslate));

  Keybind gizmoRotate{};
  gizmoRotate.id = "gizmo_rotate";
  gizmoRotate.chord.keys = {Key::E};
  gizmoRotate.chord.allowExtraKeys = true;
  gizmoRotate.priority = 3;
  gizmoRotate.enabled = canUseGizmoShortcuts;
  gizmoRotate.action = [this]() {
    if (auto *ed = m_app->editorLayer())
      ed->gizmo().op = GizmoOp::Rotate;
  };
  m_keybinds.add(std::move(gizmoRotate));

  Keybind gizmoScale{};
  gizmoScale.id = "gizmo_scale";
  gizmoScale.chord.keys = {Key::R};
  gizmoScale.chord.allowExtraKeys = true;
  gizmoScale.priority = 3;
  gizmoScale.enabled = canUseGizmoShortcuts;
  gizmoScale.action = [this]() {
    if (auto *ed = m_app->editorLayer())
      ed->gizmo().op = GizmoOp::Scale;
  };
  m_keybinds.add(std::move(gizmoScale));

  Keybind gizmoToggleMode{};
  gizmoToggleMode.id = "gizmo_toggle_mode";
  gizmoToggleMode.chord.keys = {Key::Q};
  gizmoToggleMode.chord.allowExtraKeys = true;
  gizmoToggleMode.priority = 3;
  gizmoToggleMode.enabled = canUseGizmoShortcuts;
  gizmoToggleMode.action = [this]() {
    if (auto *ed = m_app->editorLayer()) {
      auto &gizmo = ed->gizmo();
      gizmo.mode =
          (gizmo.mode == GizmoMode::Local) ? GizmoMode::World : GizmoMode::Local;
    }
  };
  m_keybinds.add(std::move(gizmoToggleMode));
}

static EntityID resolvePickEntity(EngineContext &engine, const Selection &sel,
                                  uint32_t pid) {
  EntityID e = sel.entityForPick(pid);
  if (e != InvalidEntity)
    return e;
  const uint32_t slotIndex = pickEntity(pid);
  return engine.resolveEntityIndex(slotIndex);
}

static uint32_t cycleNextSubmeshPick(World &w, Selection &sel, EntityID e) {
  const uint32_t n = w.hasMesh(e) ? w.submeshCount(e) : 1u;
  uint32_t &idx = sel.cycleIndexByEntity[e];
  if (n == 0)
    idx = 0;
  else
    idx = (idx + 1u) % n;
  return packPick(e, idx);
}

static void applyViewportPickToSelection(EngineContext &engine, uint32_t pid,
                                         bool ctrl, bool shift,
                                         Selection &sel) {
  const EntityID e = resolvePickEntity(engine, sel, pid);
  if (pid == 0u || e == InvalidEntity) {
    if (!ctrl)
      sel.clear();
    return;
  }

  if (ctrl) {
    sel.togglePick(pid, e);
  } else if (shift) {
    const uint32_t ps = pickSubmesh(pid);
    if (sel.kind == SelectionKind::Picks && sel.activeEntity == e) {
      if (sel.activePick != 0u)
        sel.cycleIndexByEntity[e] = pickSubmesh(sel.activePick);
      const uint32_t next = cycleNextSubmeshPick(engine.world(), sel, e);
      sel.setSinglePick(next, e);
    } else {
      sel.setSinglePick(pid, e);
      sel.activeEntity = e;
      sel.cycleIndexByEntity[e] = ps;
    }
  } else {
    sel.setSinglePick(pid, e);
    sel.cycleIndexByEntity[e] = pickSubmesh(pid);
  }

  sel.activeEntity = e;
}

static void buildSelectedPicksForOutline(const Selection &sel,
                                         std::vector<uint32_t> &out) {
  out.clear();
  if (sel.kind != SelectionKind::Picks || sel.picks.empty())
    return;
  out = sel.picks;
}

static void deleteSelection(World &world, Selection &sel) {
  if (sel.kind != SelectionKind::Picks || sel.picks.empty())
    return;

  // Delete by unique entities (submesh-aware selection still deletes entity)
  std::vector<EntityID> ents;
  ents.reserve(sel.picks.size());
  for (uint32_t p : sel.picks) {
    EntityID e = sel.entityForPick(p);
    if (e != InvalidEntity)
      ents.push_back(e);
  }
  std::sort(ents.begin(), ents.end(), [](EntityID a, EntityID b) {
    if (a.index != b.index)
      return a.index < b.index;
    return a.generation < b.generation;
  });
  ents.erase(std::unique(ents.begin(), ents.end()), ents.end());

  // Optional: delete children safely by descending ID
  std::sort(ents.begin(), ents.end(), [](EntityID a, EntityID b) {
    if (a.index != b.index)
      return a.index > b.index;
    return a.generation > b.generation;
  });

  for (EntityID e : ents) {
    if (world.isAlive(e))
      world.destroyEntity(e);
  }
  sel.clear();
}

static bool ancestorIsInSet(World &world, EntityID e,
                            const std::vector<EntityID> &set) {
  EntityID p = world.parentOf(e);
  while (p != InvalidEntity) {
    if (std::find(set.begin(), set.end(), p) != set.end())
      return true;
    p = world.parentOf(p);
  }
  return false;
}

static void duplicateSelection(EngineContext &engine, Selection &sel) {
  World &world = engine.world();
  if (sel.kind != SelectionKind::Picks || sel.picks.empty())
    return;

  // Duplicate top-level entities only; preserve hierarchy by cloning subtree
  std::vector<EntityID> ents;
  ents.reserve(sel.picks.size());
  for (uint32_t p : sel.picks) {
    EntityID e = sel.entityForPick(p);
    if (e != InvalidEntity)
      ents.push_back(e);
  }
  std::sort(ents.begin(), ents.end(), [](EntityID a, EntityID b) {
    if (a.index != b.index)
      return a.index < b.index;
    return a.generation < b.generation;
  });
  ents.erase(std::unique(ents.begin(), ents.end()), ents.end());

  // Filter out entities whose ancestor is also selected
  std::vector<EntityID> top;
  top.reserve(ents.size());
  for (EntityID e : ents) {
    if (!ancestorIsInSet(world, e, ents))
      top.push_back(e);
  }

  std::vector<uint32_t> newPicks;
  std::vector<EntityID> newEntities;
  for (EntityID e : top) {
    EntityID dup = world.duplicateSubtree(e, InvalidEntity, &engine.materials());
    if (dup == InvalidEntity)
      continue;

    // pick submesh0 of cloned entity if it exists, else packPick(dup,0)
    const uint32_t pid = packPick(dup, 0);
    newPicks.push_back(pid);
    newEntities.push_back(dup);
  }

  if (!newPicks.empty()) {
    sel.kind = SelectionKind::Picks;
    sel.picks = newPicks;
    sel.pickEntity.clear();
    for (size_t i = 0; i < newPicks.size(); ++i)
      sel.pickEntity.emplace(newPicks[i], newEntities[i]);
    sel.activePick = newPicks.back();
    sel.activeEntity = sel.entityForPick(sel.activePick);
  }
}

static bool imguiIniMissing() {
  ImGuiIO &io = ImGui::GetIO();
  if (!io.IniFilename || io.IniFilename[0] == 0)
    return true;
  return !std::filesystem::exists(std::filesystem::absolute(io.IniFilename));
}

static std::string projectStatePath() {
  return (std::filesystem::current_path() / ".nyx" / "project.nyxproj.json")
      .string();
}

static std::string resolveScenePath(const std::string &scenePath,
                                    const std::string &projectPath) {
  if (scenePath.empty())
    return scenePath;
  std::filesystem::path p(scenePath);
  if (p.is_relative()) {
    std::filesystem::path cwd = std::filesystem::current_path();
    std::filesystem::path cand = (cwd / p).lexically_normal();
    if (std::filesystem::exists(cand))
      return cand.string();

    std::filesystem::path base =
        std::filesystem::path(projectPath).parent_path();
    if (!base.empty()) {
      std::filesystem::path candProj = (base / p).lexically_normal();
      if (std::filesystem::exists(candProj))
        return candProj.string();
    }

    return cand.string();
  }
  return p.lexically_normal().string();
}

static void applyEditorState(EditorState &st, EditorLayer &ed,
                             EngineContext &engine) {
  auto &ps = ed.persist();
  ps.panels.viewport = st.panels.showViewport;
  ps.panels.hierarchy = st.panels.showHierarchy;
  ps.panels.inspector = st.panels.showInspector;
  ps.panels.assetBrowser = st.panels.showAssets;
  ps.panels.stats = st.panels.showStats;

  ed.gizmo().op = st.gizmoOp;
  ed.gizmo().mode = st.gizmoMode;
  ed.setAutoSave(st.autoSave);
  ed.setScenePath(st.lastScenePath);
  ed.setProjectFps(st.projectFps);
  engine.animation().setFps(st.projectFps);

  engine.setViewMode(st.viewport.viewMode);
  engine.renderer().setOutlineThicknessPx(st.viewport.outlineThicknessPx);
}

static void captureAnimationClipState(EditorState &st, EngineContext &engine) {
  const auto &world = engine.world();
  const auto &clip = engine.activeClip();
  auto &dst = st.animationClip;
  dst.valid = true;
  dst.name = clip.name;
  dst.lastFrame = std::max<AnimFrame>(0, clip.lastFrame);
  dst.loop = clip.loop;
  dst.tracks.clear();
  dst.ranges.clear();
  dst.tracks.reserve(clip.tracks.size());
  dst.ranges.reserve(clip.entityRanges.size());
  dst.nextBlockId = std::max<uint32_t>(1u, clip.nextBlockId);

  for (const auto &t : clip.tracks) {
    if (!world.isAlive(t.entity))
      continue;
    const EntityUUID u = world.uuid(t.entity);
    if (!u)
      continue;
    PersistedAnimTrack pt{};
    pt.entity = u;
    pt.blockId = t.blockId;
    pt.channel = t.channel;
    pt.curve = t.curve;
    dst.tracks.push_back(std::move(pt));
  }

  for (const auto &r : clip.entityRanges) {
    if (!world.isAlive(r.entity))
      continue;
    const EntityUUID u = world.uuid(r.entity);
    if (!u)
      continue;
    PersistedAnimRange pr{};
    pr.entity = u;
    pr.blockId = r.blockId;
    pr.start = r.start;
    pr.end = r.end;
    if (pr.end < pr.start)
      std::swap(pr.start, pr.end);
    dst.ranges.push_back(std::move(pr));
  }
}

static void restoreAnimationClipState(const EditorState &st,
                                      EngineContext &engine) {
  auto &clip = engine.activeClip();
  if (st.animationClip.valid) {
    clip.name = st.animationClip.name;
    clip.lastFrame = std::max<AnimFrame>(0, st.animationClip.lastFrame);
    clip.loop = st.animationClip.loop;
    clip.tracks.clear();
    clip.entityRanges.clear();
    clip.nextBlockId = std::max<uint32_t>(1u, st.animationClip.nextBlockId);
    clip.tracks.reserve(st.animationClip.tracks.size());
    clip.entityRanges.reserve(st.animationClip.ranges.size());

    auto &world = engine.world();
    for (const auto &t : st.animationClip.tracks) {
      if (!t.entity)
        continue;
      EntityID e = world.findByUUID(t.entity);
      if (e == InvalidEntity || !world.isAlive(e))
        continue;
      AnimTrack rt{};
      rt.entity = e;
      rt.blockId = t.blockId;
      rt.channel = t.channel;
      rt.curve = t.curve;
      clip.tracks.push_back(std::move(rt));
    }

    for (const auto &r : st.animationClip.ranges) {
      if (!r.entity)
        continue;
      EntityID e = world.findByUUID(r.entity);
      if (e == InvalidEntity || !world.isAlive(e))
        continue;
      AnimEntityRange rr{};
      rr.entity = e;
      rr.blockId = r.blockId;
      rr.start = r.start;
      rr.end = std::max<AnimFrame>(rr.start, r.end);
      clip.entityRanges.push_back(rr);
    }
    uint32_t maxBlock = 0;
    for (auto &r : clip.entityRanges)
      maxBlock = std::max(maxBlock, r.blockId);
    for (auto &t : clip.tracks)
      maxBlock = std::max(maxBlock, t.blockId);
    for (auto &r : clip.entityRanges) {
      if (r.blockId == 0)
        r.blockId = ++maxBlock;
    }
    for (auto &t : clip.tracks) {
      if (t.blockId != 0)
        continue;
      uint32_t fallback = 0;
      for (const auto &r : clip.entityRanges) {
        if (r.entity == t.entity) {
          fallback = r.blockId;
          break;
        }
      }
      if (fallback == 0)
        fallback = ++maxBlock;
      t.blockId = fallback;
    }
    clip.nextBlockId = std::max<uint32_t>(maxBlock + 1, clip.nextBlockId);
  } else {
    clip.loop = st.animationLoop;
    clip.lastFrame = std::max<AnimFrame>(0, st.animationLastFrame);
  }

  const int32_t clampedFrame =
      std::clamp<int32_t>(st.animationFrame, 0, clip.lastFrame);
  engine.animation().setFrame(clampedFrame);
  if (st.animationPlaying)
    engine.animation().play();
  else
    engine.animation().pause();
}

static void captureEditorState(EditorState &st, EditorLayer &ed,
                               EngineContext &engine) {
  const auto &ps = ed.persist();
  st.panels.showViewport = ps.panels.viewport;
  st.panels.showHierarchy = ps.panels.hierarchy;
  st.panels.showInspector = ps.panels.inspector;
  st.panels.showAssets = ps.panels.assetBrowser;
  st.panels.showStats = ps.panels.stats;

  st.gizmoOp = ed.gizmo().op;
  st.gizmoMode = ed.gizmo().mode;
  st.autoSave = ed.autoSave();
  st.lastScenePath = ed.scenePath();

  st.viewport.viewMode = engine.viewMode();
  st.viewport.outlineThicknessPx = engine.renderer().outlineThicknessPx();

  const EntityID active = engine.world().activeCamera();
  st.activeCamera = engine.world().uuid(active);
  st.uuidSeed = engine.world().uuidSeed();
  st.projectFps = ed.projectFps();
  st.animationFrame = engine.animation().frame();
  st.animationPlaying = engine.animation().playing();
  st.animationLoop = engine.activeClip().loop;
  st.animationLastFrame = std::max<int32_t>(0, engine.activeClip().lastFrame);
  captureAnimationClipState(st, engine);
  ed.sequencerPanel().capturePersistState(st.sequencer);
}

static uint64_t hashMix64(uint64_t h, uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  return h;
}

static uint64_t animationPersistFingerprint(const EngineContext &engine) {
  const auto &anim = engine.animation();
  const auto &clip = engine.activeClip();
  uint64_t h = 0xcbf29ce484222325ULL;
  h = hashMix64(h, (uint64_t)anim.frame());
  h = hashMix64(h, (uint64_t)(anim.playing() ? 1 : 0));
  h = hashMix64(h, (uint64_t)std::llround(anim.fps() * 1000.0f));
  h = hashMix64(h, (uint64_t)clip.lastFrame);
  h = hashMix64(h, (uint64_t)(clip.loop ? 1 : 0));
  h = hashMix64(h, (uint64_t)clip.name.size());
  for (char c : clip.name)
    h = hashMix64(h, (uint64_t)(uint8_t)c);
  h = hashMix64(h, (uint64_t)clip.tracks.size());
  for (const auto &t : clip.tracks) {
    h = hashMix64(h, ((uint64_t)t.entity.index << 32) | t.entity.generation);
    h = hashMix64(h, (uint64_t)t.blockId);
    h = hashMix64(h, (uint64_t)(int)t.channel);
    h = hashMix64(h, (uint64_t)(int)t.curve.interp);
    h = hashMix64(h, (uint64_t)t.curve.keys.size());
    for (const auto &k : t.curve.keys) {
      h = hashMix64(h, (uint64_t)k.frame);
      const uint32_t *pv = reinterpret_cast<const uint32_t *>(&k.value);
      const uint32_t *pinDx = reinterpret_cast<const uint32_t *>(&k.in.dx);
      const uint32_t *pinDy = reinterpret_cast<const uint32_t *>(&k.in.dy);
      const uint32_t *poutDx = reinterpret_cast<const uint32_t *>(&k.out.dx);
      const uint32_t *poutDy = reinterpret_cast<const uint32_t *>(&k.out.dy);
      h = hashMix64(h, (uint64_t)*pv);
      h = hashMix64(h, (uint64_t)*pinDx);
      h = hashMix64(h, (uint64_t)*pinDy);
      h = hashMix64(h, (uint64_t)*poutDx);
      h = hashMix64(h, (uint64_t)*poutDy);
      h = hashMix64(h, (uint64_t)(int)k.easeOut);
    }
  }
  h = hashMix64(h, (uint64_t)clip.entityRanges.size());
  for (const auto &r : clip.entityRanges) {
    h = hashMix64(h, ((uint64_t)r.entity.index << 32) | r.entity.generation);
    h = hashMix64(h, (uint64_t)r.blockId);
    h = hashMix64(h, (uint64_t)r.start);
    h = hashMix64(h, (uint64_t)r.end);
  }
  h = hashMix64(h, (uint64_t)clip.nextBlockId);
  return h;
}

int Application::run() {
  float lastT = static_cast<float>(m_app->window().getTimeSeconds());
  float projectSaveTimer = 0.0f;

  const std::string projPath = projectStatePath();
  m_editorState.lastProjectPath = projPath;
  ProjectSerializer::loadFromFile(m_editorState, projPath);

  m_engine->world().setUUIDSeed(m_editorState.uuidSeed);

  if (m_app->editorLayer()) {
    m_app->editorLayer()->setWorld(&m_engine->world());
    applyEditorState(m_editorState, *m_app->editorLayer(), *m_engine);
  }

  bool loadedScene = false;

  if (!m_editorState.lastScenePath.empty()) {
    const std::string resolvedPath =
        resolveScenePath(m_editorState.lastScenePath, projPath);

    if (std::filesystem::exists(resolvedPath)) {
      m_engine->resetMaterials();
      loadedScene = WorldSerializer::loadFromFile(
          m_engine->world(), m_engine->materials(), resolvedPath);

      if (loadedScene) {
        EditorStateIO::onSceneOpened(m_editorState, resolvedPath);
        if (m_app->editorLayer()) {
          m_app->editorLayer()->setScenePath(resolvedPath);
          m_app->editorLayer()->setWorld(&m_engine->world());
        }
        const auto &sky = m_engine->world().skySettings();
        if (!sky.hdriPath.empty()) {
          m_engine->envIBL().loadFromHDR(sky.hdriPath);
        }
      } else {
        Log::Warn("Failed to load scene from '{}'", resolvedPath);
      }
    } else {
      Log::Warn("Scene file does not exist: '{}'", resolvedPath);
    }
  } else {
    Log::Info("No last scene path configured");
  }

  if (loadedScene) {
    m_engine->rebuildEntityIndexMap();
    m_engine->rebuildRenderables();
    if (m_app->editorLayer())
      m_app->editorLayer()->setSceneLoaded(true);
  } else if (m_app->editorLayer()) {
    m_app->editorLayer()->defaultScene(*m_engine);
  }

  // Editor camera entity (ECS-driven)
  if (loadedScene && m_editorState.activeCamera) {
    EntityID cam = m_engine->world().findByUUID(m_editorState.activeCamera);
    if (cam != InvalidEntity && m_engine->world().hasCamera(cam)) {
      m_engine->world().setActiveCamera(cam);
      if (m_app->editorLayer())
        m_app->editorLayer()->setCameraEntity(cam);
    }
  }

  restoreAnimationClipState(m_editorState, *m_engine);
  if (m_app->editorLayer()) {
    auto *ed = m_app->editorLayer();
    ed->sequencerPanel().setWorld(ed->world());
    ed->sequencerPanel().setAnimationSystem(&m_engine->animation());
    ed->sequencerPanel().setAnimationClip(&m_engine->activeClip());
    ed->sequencerPanel().applyPersistState(m_editorState.sequencer);
  }
  uint64_t lastProjectFingerprint = animationPersistFingerprint(*m_engine);

  while (!m_app->window().shouldClose()) {
    const float nowT = static_cast<float>(m_app->window().getTimeSeconds());
    const float dt = std::max(0.0f, nowT - lastT);
    lastT = nowT;
    projectSaveTimer += dt;

    // ------------------------------------------------------------
    // Begin frame
    // ------------------------------------------------------------
    auto &win = m_app->window();
    auto &input = win.input();
    input.beginFrame();
    if (win.isMinimized() || !win.isVisible()) {
      // Keep pumping events while minimized/hidden to avoid OS "not
      // responding".
      win.waitEventsTimeout(0.1);
      input.endFrame();
      lastT = static_cast<float>(m_app->window().getTimeSeconds());
      continue;
    }
    m_app->beginFrame();

    // ------------------------------------------------------------
    // Global toggles
    // ------------------------------------------------------------
    if (input.isPressed(Key::F)) {
      // Contract: F toggles editor overlay by removing/adding editor layer.
      // IMPORTANT: preserve editor state inside EditorLayer (dock layout, etc).
      const bool wasVisible = m_app->isEditorVisible();
      m_app->toggleEditorOverlay();

      // When hiding editor, release capture so it doesn't "go insane"
      if (wasVisible && !m_app->isEditorVisible()) {
        // m_app->editorLayer()->cameraController().captureMouse(false, win);
      }

      // When showing editor again, also ensure capture is released
      // (user should RMB-capture explicitly inside viewport).
      if (!wasVisible && m_app->isEditorVisible()) {
        // m_app->editorLayer()->cameraController().captureMouse(false, win);

        if (m_app->editorLayer())
          m_app->editorLayer()->setWorld(&m_engine->world());
      }
    }

    if (input.isPressed(Key::Escape)) {
      // ESC always releases mouse capture
      // m_app->editorLayer()->cameraController().captureMouse(false, win);
    }

    // ------------------------------------------------------------
    // Build UI (editor visible)
    // ------------------------------------------------------------
    if (m_app->isEditorVisible()) {
      m_app->imguiBegin();

      ImGuiWindowFlags flags =
          ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
      flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
      flags |=
          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

      const ImGuiViewport *vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(vp->WorkPos);
      ImGui::SetNextWindowSize(vp->WorkSize);
      ImGui::SetNextWindowViewport(vp->ID);

      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::Begin("NyxDockspace", nullptr, flags);
      ImGui::PopStyleVar(2);

      ImGuiID dockspaceId = ImGui::GetID("NyxDockspaceID");
      m_engine->setDockspaceID(dockspaceId);
      ImGui::DockSpace(dockspaceId, ImVec2(0, 0),
                       ImGuiDockNodeFlags_PassthruCentralNode);

      if (auto *ed = m_app->editorLayer()) {
        if (imguiIniMissing())
          m_editorState.dockFallbackApplied = false;
        DockspaceLayout::applyDefaultLayoutIfNeeded(m_editorState, dockspaceId);
      }

      for (auto &layer : m_app->layers()) {
        layer->onImGui(*m_engine);
      }

      if (auto *ed = m_app->editorLayer()) {
        const std::string &scenePath = ed->scenePath();
        if (!scenePath.empty() && scenePath != m_editorState.lastScenePath) {
          m_editorState.lastScenePath = scenePath;
          m_editorState.pushRecentScene(scenePath);
          // Scene changed (e.g. Open Scene): apply persisted clip to the
          // newly loaded world before capturing/saving state again.
          restoreAnimationClipState(m_editorState, *m_engine);
          captureEditorState(m_editorState, *ed, *m_engine);
          ProjectSerializer::saveToFile(m_editorState,
                                        m_editorState.lastProjectPath);
          lastProjectFingerprint = animationPersistFingerprint(*m_engine);
        }
        m_editorState.autoSave = ed->autoSave();
        if (!ed->sceneLoaded()) {
          m_editorState.lastScenePath.clear();
        }

        // Persist project-level editor/animation state even if the scene path
        // is unchanged (e.g. keyframe edits in sequencer).
        if (projectSaveTimer >= 0.75f) {
          projectSaveTimer = 0.0f;
          const uint64_t nowFp = animationPersistFingerprint(*m_engine);
          if (nowFp != lastProjectFingerprint) {
            captureEditorState(m_editorState, *ed, *m_engine);
            EditorStateIO::sanitizeBeforeSave(m_editorState);
            ProjectSerializer::saveToFile(m_editorState,
                                          m_editorState.lastProjectPath);
            lastProjectFingerprint = nowFp;
          }
        }
      }

      ImGui::End(); // Dockspace host
    }

    // ------------------------------------------------------------
    // Capture rules + viewport gating
    // ------------------------------------------------------------
    bool imguiWantsText = false;
    bool viewportHovered = false;
    bool viewportFocused = false;
    bool gizmoWantsMouse = false;

    if (m_app->isEditorVisible()) {
      ImGuiIO &io = ImGui::GetIO();
      imguiWantsText = io.WantTextInput;

      if (auto *ed = m_app->editorLayer()) {
        viewportHovered = ed->viewport().hovered; // SHOULD be image-hovered
        viewportFocused = ed->viewport().focused;
        gizmoWantsMouse = ed->gizmoWantsMouse();
      }
    } else {
      viewportHovered = true;
      viewportFocused = true;
    }

    // RMB capture allowed only in viewport and when not typing
    const bool allowRmbCapture =
        viewportHovered && (!m_app->isEditorVisible() || !imguiWantsText) &&
        !gizmoWantsMouse;

    // if (allowRmbCapture && input.isPressed(Key::MouseRight)) {
    //   m_app->editorLayer()->cameraController().captureMouse(true, win);
    // }
    // if (input.isReleased(Key::MouseRight)) {
    //   m_app->editorLayer()->cameraController().captureMouse(false, win);
    // }

    // ------------------------------------------------------------
    // Camera movement
    // ------------------------------------------------------------
    if ((viewportHovered) && m_app->editorLayer()) {
      m_app->editorLayer()->cameraController().tick(*m_engine, *m_app, dt);
    }

    // ------------------------------------------------------------
    // Keybinds (editor only; ignore when typing)
    // ------------------------------------------------------------
    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      ImGuiIO &io = ImGui::GetIO();
      if (!io.WantTextInput) {
        if (!m_engine->uiBlockGlobalShortcuts()) {
          m_keybinds.process(input);
        }
        if (auto *ed = m_app->editorLayer()) {
          const bool seqHot = ed->sequencerPanel().timelineHot();
          if (seqHot)
            ed->sequencerPanel().handleStepRepeat(input, dt);
          if (input.isPressed(Key::Space)) {
            if (seqHot)
              ed->sequencerPanel().togglePlay();
            else
              m_engine->animation().toggle();
          }
        }
      }
    } else {
      // Fullscreen/game view fallback: keep Space playback toggle available
      // even when editor UI/keybind routing is not active.
      if (input.isPressed(Key::Space)) {
        m_engine->animation().toggle();
      }
    }

    // ------------------------------------------------------------
    // Click-to-pick (editor viewport only)
    // ------------------------------------------------------------
    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      auto &vp = m_app->editorLayer()->viewport();
      const bool rmbCaptured =
          m_app->editorLayer()->cameraController().mouseCaptured;

      ImGuiIO &io = ImGui::GetIO();
      const bool canPick = vp.hovered && vp.hasImageRect() && !rmbCaptured &&
                           !io.WantTextInput &&
                           !m_app->editorLayer()->gizmoWantsMouse();

      if (canPick && input.isPressed(Key::MouseLeft)) {
        const double mx = input.state().mouseX;
        const double my = input.state().mouseY;

        ViewportImageRect r{};
        r.imageMin = {vp.imageMin.x, vp.imageMin.y};
        r.imageMax = {vp.imageMax.x, vp.imageMax.y};
        if (vp.lastRenderedSize.x > 0 && vp.lastRenderedSize.y > 0) {
          r.renderedSize = {vp.lastRenderedSize.x, vp.lastRenderedSize.y};
        } else {
          r.renderedSize = {vp.desiredSize.x, vp.desiredSize.y};
        }

        const auto pick = mapMouseToFramebufferPixel(mx, my, r);
        if (pick.inside) {
          const uint32_t px = pick.px;
          const uint32_t py = pick.py;
          m_engine->requestPick(px, py);
          m_pendingViewportPick = true;
          m_pendingPickCtrl = isCtrlDown(input);
          m_pendingPickShift = isShiftDown(input);
        }
      }
    }

    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      m_app->editorLayer()->syncWorldEvents(*m_engine);
    }

    // ------------------------------------------------------------
    // Engine tick
    // ------------------------------------------------------------
    if (!m_app->isEditorVisible() && m_app->editorLayer()) {
      auto *ed = m_app->editorLayer();
      ed->sequencerPanel().setWorld(ed->world());
      ed->sequencerPanel().setAnimationSystem(&m_engine->animation());
      ed->sequencerPanel().setAnimationClip(&m_engine->activeClip());
      if (ed->world()) {
        std::vector<EntityID> exclude;
        exclude.push_back(ed->cameraEntity());
        exclude.push_back(ed->world()->activeCamera());
        ed->sequencerPanel().setHiddenExclusions(exclude);
        ed->sequencerPanel().setTrackExclusions(exclude);
      }
      ed->sequencerPanel().updateHiddenEntities();
      m_engine->setHiddenEntities(ed->sequencerPanel().hiddenEntities());
    }
    m_engine->tick(dt);
    ;
    // ------------------------------------------------------------
    // Render camera override (editor viewport)
    // ------------------------------------------------------------
    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      auto *ed = m_app->editorLayer();
      EntityID renderCam = InvalidEntity;
      if (ed->viewThroughCamera()) {
        const EntityID active = m_engine->world().activeCamera();
        if (active != InvalidEntity && m_engine->world().hasCamera(active))
          renderCam = active;
      }
      if (renderCam == InvalidEntity) {
        const EntityID editorCam = ed->cameraEntity();
        if (editorCam != InvalidEntity &&
            m_engine->world().hasCamera(editorCam))
          renderCam = editorCam;
      }
      m_engine->setRenderCameraOverride(renderCam);
      if (ed->viewThroughCamera() && renderCam != InvalidEntity)
        m_engine->setHiddenEntity(renderCam);
      else
        m_engine->setHiddenEntity(InvalidEntity);
    } else {
      m_engine->setRenderCameraOverride(InvalidEntity);
      const EntityID active = m_engine->world().activeCamera();
      if (active != InvalidEntity && m_engine->world().hasCamera(active))
        m_engine->setHiddenEntity(active);
      else
        m_engine->setHiddenEntity(InvalidEntity);
    }
    // ------------------------------------------------------------
    // Determine render target size
    // ------------------------------------------------------------
    uint32_t renderW = win.width();
    uint32_t renderH = win.height();

    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      const auto &vp = m_app->editorLayer()->viewport();
      renderW = std::max(1u, vp.desiredSize.x);
      renderH = std::max(1u, vp.desiredSize.y);
    }

    // ------------------------------------------------------------
    // Render lambda (uses selection picks for outline)
    // ------------------------------------------------------------
    auto renderScene = [&](uint32_t w, uint32_t h) -> uint32_t {
      std::vector<uint32_t> selPick;

      if (m_app->isEditorVisible() && m_app->editorLayer()) {
        const auto &sel = m_app->editorLayer()->selection();
        buildSelectedPicksForOutline(sel, selPick);
      }

      uint32_t activePick = 0u;
      if (m_app->isEditorVisible() && m_app->editorLayer()) {
        const auto &sel = m_app->editorLayer()->selection();
        activePick = sel.activePick;
      }
      m_engine->setSelectionPickIDs(selPick, activePick);
      const uint32_t windowW = win.width();
      const uint32_t windowH = win.height();
      uint32_t viewportW = w;
      uint32_t viewportH = h;
      if (m_app->isEditorVisible() && m_app->editorLayer()) {
        const auto &vp = m_app->editorLayer()->viewport();
        viewportW = std::max(1u, vp.desiredSize.x);
        viewportH = std::max(1u, vp.desiredSize.y);
      }
      return m_engine->render(windowW, windowH, viewportW, viewportH, w, h,
                              m_app->isEditorVisible());
    };

    uint32_t viewportTex = renderScene(renderW, renderH);

    // ------------------------------------------------------------
    // Resolve pending pick AFTER render (pick pass writes ID texture)
    // ------------------------------------------------------------
    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      auto &sel = m_app->editorLayer()->selection();

      if (m_pendingViewportPick) {
        m_pendingViewportPick = false;

        const uint32_t pid = m_engine->lastPickedID(); // packed entity+submesh
        applyViewportPickToSelection(*m_engine, pid, m_pendingPickCtrl,
                                     m_pendingPickShift, sel);

        // Re-render immediately so outline matches selection this frame
        viewportTex = renderScene(renderW, renderH);
      }
    }

    // ------------------------------------------------------------
    // Submit texture to viewport
    // ------------------------------------------------------------
    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      m_app->editorLayer()->setViewportTexture(viewportTex);
      m_app->editorLayer()->viewport().lastRenderedSize = {renderW, renderH};
    }

    // ------------------------------------------------------------
    // Finalize ImGui
    // ------------------------------------------------------------
    if (m_app->isEditorVisible()) {
      m_app->imguiEnd();
    }

    // ------------------------------------------------------------
    // End frame
    // ------------------------------------------------------------
    input.endFrame();
    m_app->endFrame();
  }

  if (m_app->editorLayer())
    captureEditorState(m_editorState, *m_app->editorLayer(), *m_engine);
  EditorStateIO::sanitizeBeforeSave(m_editorState);
  ProjectSerializer::saveToFile(m_editorState, m_editorState.lastProjectPath);

  return 0;
}

} // namespace Nyx
