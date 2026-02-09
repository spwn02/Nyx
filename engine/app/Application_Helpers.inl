static bool isShiftDown(const InputSystem &in) {
  return in.isDown(Key::LeftShift) || in.isDown(Key::RightShift);
}
static bool isCtrlDown(const InputSystem &in) {
  return in.isDown(Key::LeftCtrl) || in.isDown(Key::RightCtrl);
}

static void deleteSelection(World &world, Selection &sel);
static void duplicateSelection(EngineContext &engine, Selection &sel);
static void captureEditorState(EditorState &st, EditorLayer &ed,
                               EngineContext &engine);
static void syncProjectFromEditorState(NyxProjectRuntime &runtime,
                                       const EditorState &st);

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
  save.id = "save_project";
  save.chord.keys = {Key::S};
  save.chord.mods = KeyMod::Ctrl;
  save.chord.allowExtraKeys = false;
  save.priority = 10;
  save.enabled = canUseEditorShortcuts;
  save.action = [this]() {
    if (!m_projectManager.hasProject())
      return;
    const bool scenesSaved = m_sceneManager.saveAllProjectScenes();
    if (auto *ed = m_app->editorLayer()) {
      if (scenesSaved)
        ed->markSceneClean(*m_engine);
      captureEditorState(m_editorState, *ed, *m_engine);
      EditorStateIO::sanitizeBeforeSave(m_editorState);
      syncProjectFromEditorState(m_projectManager.runtime(), m_editorState);
    }
    (void)m_projectManager.runtime().saveProject(
        m_projectManager.runtime().projectFileAbs());
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
  quit.action = [this]() { m_requestClose = true; };
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

static std::filesystem::path cacheRootPath() {
  return (std::filesystem::current_path() / ".cache").lexically_normal();
}

static std::string editorUserConfigPath() {
  return (cacheRootPath() / "editor_user.nyxu").string();
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

static std::string toProjectRelativePath(const NyxProjectRuntime &runtime,
                                         const std::string &path) {
  if (path.empty())
    return {};
  std::filesystem::path p(path);
  if (p.is_relative())
    return p.lexically_normal().string();
  if (!runtime.hasProject())
    return p.lexically_normal().string();

  std::error_code ec;
  const std::filesystem::path rel =
      std::filesystem::relative(p, runtime.projectDirAbs(), ec);
  if (ec || rel.empty())
    return p.lexically_normal().string();

  const std::string relStr = rel.lexically_normal().string();
  if (relStr.rfind("..", 0) == 0)
    return p.lexically_normal().string();
  return relStr;
}

static void syncProjectFromEditorState(NyxProjectRuntime &runtime,
                                       const EditorState &st) {
  if (!runtime.hasProject())
    return;
  NyxProject &proj = runtime.proj();
  proj.settings.exposure = st.viewport.exposure;

  const std::string relScene = toProjectRelativePath(runtime, st.lastScenePath);
  proj.settings.startupScene = relScene;

  if (relScene.empty())
    return;

  auto it = std::find_if(
      proj.scenes.begin(), proj.scenes.end(),
      [&](const NyxProjectSceneEntry &entry) { return entry.relPath == relScene; });
  if (it == proj.scenes.end()) {
    NyxProjectSceneEntry entry{};
    entry.relPath = relScene;
    entry.name = std::filesystem::path(relScene).stem().string();
    proj.scenes.push_back(std::move(entry));
    it = std::prev(proj.scenes.end());
  }

  // Keep last-opened/startup scene first in list.
  if (it != proj.scenes.begin())
    std::rotate(proj.scenes.begin(), it, std::next(it));
}

static void syncEditorStateFromProject(EditorState &st,
                                       const NyxProjectRuntime &runtime) {
  if (!runtime.hasProject())
    return;
  const NyxProject &proj = runtime.proj();
  st.viewport.exposure = proj.settings.exposure;
  if (!proj.settings.startupScene.empty())
    st.lastScenePath = runtime.resolveAbs(proj.settings.startupScene);
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
