#include "EditorHistory.h"

#include "animation/AnimationSystem.h"
#include "scene/World.h"
#include "scene/Pick.h"
#include "scene/JsonLite.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace Nyx {

static double nowSeconds() {
  using clock = std::chrono::steady_clock;
  static const auto start = clock::now();
  const auto t = clock::now();
  return std::chrono::duration<double>(t - start).count();
}

static bool vecEq(const std::vector<uint32_t> &a,
                  const std::vector<uint32_t> &b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin());
}

static bool vecEqEnt(const std::vector<EntityID> &a,
                     const std::vector<EntityID> &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i])
      return false;
  }
  return true;
}

static bool categoriesEqual(const CategorySnapshot &a,
                            const CategorySnapshot &b) {
  if (a.categories.size() != b.categories.size())
    return false;
  for (size_t i = 0; i < a.categories.size(); ++i) {
    const auto &ca = a.categories[i];
    const auto &cb = b.categories[i];
    if (ca.name != cb.name || ca.parent != cb.parent)
      return false;
    if (!vecEq(ca.children, cb.children))
      return false;
    if (!vecEqEnt(ca.entities, cb.entities))
      return false;
  }
  if (a.entityCategoriesByUUID.size() != b.entityCategoriesByUUID.size())
    return false;
  for (const auto &kv : a.entityCategoriesByUUID) {
    auto it = b.entityCategoriesByUUID.find(kv.first);
    if (it == b.entityCategoriesByUUID.end())
      return false;
    if (!vecEq(kv.second, it->second))
      return false;
  }
  return true;
}

static bool animCurvesEqual(const AnimCurve &a, const AnimCurve &b) {
  if (a.interp != b.interp || a.keys.size() != b.keys.size())
    return false;
  for (size_t i = 0; i < a.keys.size(); ++i) {
    const auto &ka = a.keys[i];
    const auto &kb = b.keys[i];
    if (ka.frame != kb.frame || ka.value != kb.value ||
        ka.in.dx != kb.in.dx || ka.in.dy != kb.in.dy ||
        ka.out.dx != kb.out.dx || ka.out.dy != kb.out.dy ||
        ka.easeOut != kb.easeOut)
      return false;
  }
  return true;
}

void EditorHistory::setWorld(World *world, MaterialSystem *materials) {
  if (world == m_world && materials == m_materials)
    return;
  m_world = world;
  m_materials = materials;
  m_entries.clear();
  m_cursor = -1;
  m_nextId = 1;
  m_cacheById.clear();
  if (m_world) {
    rebuildCache(*m_world);
    m_lastCategories = captureCategories(*m_world);
    m_lastSky = m_world->skySettings();
  }
  if (m_materials) {
    m_lastMaterialSerial = m_materials->changeSerial();
    m_materials->snapshot(m_lastMaterials);
  }
  m_loadedFromDisk = false;
  m_transformBatchActive = false;
  m_transformBatchBefore.clear();
  m_transformBatchAfter.clear();
  if (m_world && m_anim && m_animClip)
    m_lastAnimation = captureAnimationState(*m_world);
  else
    m_lastAnimation = PersistedAnimationStateHist{};
}

void EditorHistory::setAnimationContext(AnimationSystem *anim,
                                        AnimationClip *clip) {
  if (m_anim == anim && m_animClip == clip)
    return;
  m_anim = anim;
  m_animClip = clip;
  if (m_world && m_anim && m_animClip)
    m_lastAnimation = captureAnimationState(*m_world);
  else
    m_lastAnimation = PersistedAnimationStateHist{};
}

void EditorHistory::beginTransformBatch(const std::string &label,
                                        const World &world,
                                        const Selection &sel) {
  if (m_transformBatchActive || m_applying || !m_recording)
    return;
  m_transformBatchActive = true;
  m_transformBatchLabel = label.empty() ? "Transform" : label;
  m_transformBatchBefore.clear();
  m_transformBatchAfter.clear();
  m_transformBatchSelection = captureSelection(world, sel);
}

void EditorHistory::endTransformBatch(const World &world,
                                      const Selection &sel) {
  if (!m_transformBatchActive)
    return;
  m_transformBatchActive = false;

  HistoryEntry entry{};
  entry.id = m_nextId++;
  entry.timestampSec = nowSeconds();
  entry.label = m_transformBatchLabel;
  entry.selection = captureSelection(world, sel);

  for (const auto &kv : m_transformBatchBefore) {
    auto it = m_transformBatchAfter.find(kv.first);
    if (it == m_transformBatchAfter.end())
      continue;
    OpTransform op{};
    op.uuid = kv.first;
    op.before = kv.second;
    op.after = it->second;
    entry.ops.emplace_back(op);
  }

  m_transformBatchBefore.clear();
  m_transformBatchAfter.clear();

  if (entry.ops.empty()) {
    rebuildCache(world);
    return;
  }

  if (m_cursor + 1 < (int)m_entries.size())
    m_entries.erase(m_entries.begin() + (m_cursor + 1), m_entries.end());

  m_entries.push_back(std::move(entry));
  m_cursor = (int)m_entries.size() - 1;
  if (m_entries.size() > m_maxEntries) {
    const size_t toDrop = m_entries.size() - m_maxEntries;
    m_entries.erase(m_entries.begin(), m_entries.begin() + (ptrdiff_t)toDrop);
    m_cursor = std::max(-1, m_cursor - (int)toDrop);
  }
  rebuildCache(world);
}

EditorHistory::EntityState EditorHistory::buildState(const World &world,
                                                     EntityID e) const {
  EntityState s{};
  s.uuid = world.uuid(e);
  const EntityID p = world.parentOf(e);
  s.parent = (p != InvalidEntity) ? world.uuid(p) : EntityUUID{};
  s.name = world.name(e);
  s.transform = world.transform(e);
  s.hasMesh = world.hasMesh(e);
  if (s.hasMesh)
    s.mesh = world.mesh(e);
  s.hasCamera = world.hasCamera(e);
  if (s.hasCamera) {
    s.camera = world.camera(e);
    s.cameraMatrices = world.cameraMatrices(e);
  }
  s.hasLight = world.hasLight(e);
  if (s.hasLight)
    s.light = world.light(e);
  s.hasSky = world.hasSky(e);
  if (s.hasSky)
    s.sky = world.sky(e);
  if (const auto *cats = world.entityCategories(e)) {
    s.categories = *cats;
  }
  return s;
}

void EditorHistory::rebuildCache(const World &world) {
  m_cacheById.clear();
  for (EntityID e : world.alive()) {
    if (!world.isAlive(e))
      continue;
    const EntityState s = buildState(world, e);
    m_cacheById.emplace(e, s);
  }
}

HistorySelectionSnapshot
EditorHistory::captureSelection(const World &world,
                                const Selection &sel) const {
  HistorySelectionSnapshot snap{};
  snap.kind = sel.kind;
  snap.activeMaterial = sel.activeMaterial;
  if (sel.kind == SelectionKind::Picks) {
    for (uint32_t p : sel.picks) {
      EntityID e = sel.entityForPick(p);
      if (e == InvalidEntity)
        continue;
      EntityUUID u = world.uuid(e);
      if (!u)
        continue;
      snap.picks.emplace_back(u, pickSubmesh(p));
    }
    if (sel.activePick) {
      EntityID e = sel.entityForPick(sel.activePick);
      if (e != InvalidEntity) {
        EntityUUID u = world.uuid(e);
        if (u)
          snap.activePick = {u, pickSubmesh(sel.activePick)};
      }
    }
    if (sel.activeEntity != InvalidEntity) {
      const EntityUUID u = world.uuid(sel.activeEntity);
      if (u)
        snap.activeEntity = u;
    }
  }
  return snap;
}

void EditorHistory::restoreSelection(const World &world, Selection &sel,
                                     const HistorySelectionSnapshot &snap) const {
  sel.clear();
  sel.kind = snap.kind;
  sel.activeMaterial = snap.activeMaterial;
  if (snap.kind == SelectionKind::Picks) {
    for (auto &p : snap.picks) {
      EntityID e = world.findByUUID(p.first);
      if (e == InvalidEntity)
        continue;
      const uint32_t pid = packPick(e, p.second);
      sel.picks.push_back(pid);
      sel.pickEntity.emplace(pid, e);
    }
    if (snap.activePick.first) {
      EntityID e = world.findByUUID(snap.activePick.first);
      if (e != InvalidEntity)
        sel.activePick = packPick(e, snap.activePick.second);
    }
    if (snap.activeEntity) {
      sel.activeEntity = world.findByUUID(snap.activeEntity);
    }
  }
}

CategorySnapshot EditorHistory::captureCategories(const World &world) const {
  CategorySnapshot snap{};
  snap.categories = world.categories();
  for (EntityID e : world.alive()) {
    if (!world.isAlive(e))
      continue;
    EntityUUID u = world.uuid(e);
    if (!u)
      continue;
    if (const auto *cats = world.entityCategories(e)) {
      snap.entityCategoriesByUUID[u.value] = *cats;
    }
  }
  return snap;
}

void EditorHistory::applyCategories(World &world,
                                    const CategorySnapshot &snap) const {
  // Remove all categories
  for (int i = (int)world.categories().size() - 1; i >= 0; --i)
    world.removeCategory((uint32_t)i);

  // Rebuild categories
  std::vector<uint32_t> oldToNew;
  oldToNew.reserve(snap.categories.size());
  for (const auto &c : snap.categories) {
    uint32_t idx = world.addCategory(c.name);
    oldToNew.push_back(idx);
  }
  // parents
  for (size_t i = 0; i < snap.categories.size(); ++i) {
    int32_t p = snap.categories[i].parent;
    if (p >= 0 && p < (int32_t)oldToNew.size()) {
      world.setCategoryParent(oldToNew[i], (int32_t)oldToNew[(size_t)p]);
    }
  }
  // entity assignments
  for (auto &kv : snap.entityCategoriesByUUID) {
    EntityID e = world.findByUUID(EntityUUID{kv.first});
    if (e == InvalidEntity)
      continue;
    world.clearEntityCategories(e);
    for (uint32_t oldIdx : kv.second) {
      if (oldIdx < oldToNew.size())
        world.addEntityCategory(e, (int32_t)oldToNew[oldIdx]);
    }
  }
}

EntityID EditorHistory::restoreEntity(World &world,
                                      const EntitySnapshot &snap) const {
  if (!snap.uuid)
    return InvalidEntity;
  EntityID existing = world.findByUUID(snap.uuid);
  if (existing != InvalidEntity)
    return existing;

  EntityID e = world.createEntityWithUUID(snap.uuid, snap.name.name);
  if (e == InvalidEntity)
    return InvalidEntity;

  // Parent assignment (if parent exists)
  if (snap.parent) {
    EntityID p = world.findByUUID(snap.parent);
    if (p != InvalidEntity)
      world.setParent(e, p);
  }

  world.transform(e) = snap.transform;
  world.worldTransform(e).dirty = true;
  if (snap.hasMesh) {
    world.ensureMesh(e) = snap.mesh;
  }
  if (snap.hasCamera) {
    world.ensureCamera(e) = snap.camera;
    world.cameraMatrices(e) = snap.cameraMatrices;
  }
  if (snap.hasLight) {
    world.ensureLight(e) = snap.light;
  }
  if (snap.hasSky) {
    world.ensureSky(e) = snap.sky;
  }
  // categories
  if (!snap.categories.empty()) {
    world.clearEntityCategories(e);
    for (uint32_t idx : snap.categories)
      world.addEntityCategory(e, (int32_t)idx);
  }
  return e;
}

std::string EditorHistory::labelForEvents(const WorldEvents &ev,
                                          bool categoriesChanged,
                                          bool materialsChanged) const {
  if (materialsChanged)
    return "Materials";
  if (categoriesChanged)
    return "Categories";
  if (ev.events().empty())
    return "Edit";
  const WorldEventType t = ev.events().front().type;
  switch (t) {
  case WorldEventType::TransformChanged:
    return "Transform";
  case WorldEventType::NameChanged:
    return "Rename";
  case WorldEventType::ParentChanged:
    return "Reparent";
  case WorldEventType::MeshChanged:
    return "Mesh";
  case WorldEventType::EntityCreated:
    return "Create Entity";
  case WorldEventType::EntityDestroyed:
    return "Delete Entity";
  case WorldEventType::LightChanged:
    return "Light";
  case WorldEventType::CameraCreated:
  case WorldEventType::CameraDestroyed:
    return "Camera";
  case WorldEventType::ActiveCameraChanged:
    return "Active Camera";
  case WorldEventType::SkyChanged:
    return "Sky";
  default:
    return "Edit";
  }
}

std::string EditorHistory::labelForAnimationOp(const OpAnimation &op) const {
  const auto &a = op.before;
  const auto &b = op.after;
  if (!a.valid && b.valid)
    return "Animation: Create Clip";
  if (a.valid && !b.valid)
    return "Animation: Clear Clip";
  if (!a.valid && !b.valid)
    return "Animation";

  if (a.actions.size() != b.actions.size())
    return (b.actions.size() > a.actions.size()) ? "Animation: Add Action"
                                                 : "Animation: Remove Action";
  if (a.strips.size() != b.strips.size())
    return (b.strips.size() > a.strips.size()) ? "Animation: Add Strip"
                                               : "Animation: Remove Strip";
  if (a.ranges.size() != b.ranges.size())
    return "Animation: Layer Range";
  if (a.tracks.size() != b.tracks.size())
    return (b.tracks.size() > a.tracks.size()) ? "Animation: Add Track"
                                               : "Animation: Remove Track";

  if (a.fps != b.fps)
    return "Animation: FPS";
  if (a.lastFrame != b.lastFrame)
    return "Animation: Last Frame";
  if (a.loop != b.loop)
    return "Animation: Loop";
  if (a.playing != b.playing)
    return b.playing ? "Animation: Play" : "Animation: Pause";
  if (a.frame != b.frame)
    return "Animation: Frame";

  for (size_t i = 0; i < a.tracks.size() && i < b.tracks.size(); ++i) {
    const auto &ta = a.tracks[i];
    const auto &tb = b.tracks[i];
    if (ta.entity != tb.entity || ta.blockId != tb.blockId ||
        ta.channel != tb.channel)
      return "Animation: Track Edit";
    if (!animCurvesEqual(ta.curve, tb.curve))
      return "Animation: Keyframes";
  }
  for (size_t i = 0; i < a.strips.size() && i < b.strips.size(); ++i) {
    const auto &sa = a.strips[i];
    const auto &sb = b.strips[i];
    if (sa.action != sb.action || sa.target != sb.target)
      return "Animation: Strip Target";
    if (sa.start != sb.start || sa.end != sb.end)
      return "Animation: Strip Move/Trim";
    if (sa.inFrame != sb.inFrame || sa.outFrame != sb.outFrame)
      return "Animation: Strip In/Out";
    if (sa.layer != sb.layer)
      return "Animation: Strip Layer";
    if (sa.muted != sb.muted)
      return "Animation: Strip Mute";
    if (sa.blend != sb.blend)
      return "Animation: Strip Blend";
    if (sa.timeScale != sb.timeScale)
      return "Animation: Strip Speed";
    if (sa.influence != sb.influence)
      return "Animation: Strip Influence";
    if (sa.reverse != sb.reverse)
      return "Animation: Strip Reverse";
    if (sa.fadeIn != sb.fadeIn || sa.fadeOut != sb.fadeOut)
      return "Animation: Strip Fade";
  }

  return "Animation";
}

std::string EditorHistory::labelForEntry(const HistoryEntry &entry,
                                         const World &world) const {
  if (entry.ops.empty())
    return "Edit";

  int createN = 0;
  int destroyN = 0;
  int trN = 0;
  int nameN = 0;
  int parentN = 0;
  int meshN = 0;
  int lightN = 0;
  int cameraN = 0;
  int skyN = 0;
  int activeCamN = 0;
  int catN = 0;
  int matN = 0;
  int animN = 0;
  std::string firstEntityName;
  std::string firstAnimLabel;

  auto rememberEntityName = [&](EntityUUID u) {
    if (!firstEntityName.empty() || !u)
      return;
    EntityID e = world.findByUUID(u);
    if (e != InvalidEntity && world.isAlive(e))
      firstEntityName = world.name(e).name;
  };

  for (const auto &opv : entry.ops) {
    std::visit(
        [&](auto &&op) {
          using T = std::decay_t<decltype(op)>;
          if constexpr (std::is_same_v<T, OpEntityCreate>) {
            ++createN;
            if (firstEntityName.empty())
              firstEntityName = op.snap.name.name;
          } else if constexpr (std::is_same_v<T, OpEntityDestroy>) {
            ++destroyN;
            if (firstEntityName.empty())
              firstEntityName = op.snap.name.name;
          } else if constexpr (std::is_same_v<T, OpTransform>) {
            ++trN;
            rememberEntityName(op.uuid);
          } else if constexpr (std::is_same_v<T, OpName>) {
            ++nameN;
            if (firstEntityName.empty())
              firstEntityName = op.after.empty() ? op.before : op.after;
          } else if constexpr (std::is_same_v<T, OpParent>) {
            ++parentN;
            rememberEntityName(op.uuid);
          } else if constexpr (std::is_same_v<T, OpMesh>) {
            ++meshN;
            rememberEntityName(op.uuid);
          } else if constexpr (std::is_same_v<T, OpLight>) {
            ++lightN;
            rememberEntityName(op.uuid);
          } else if constexpr (std::is_same_v<T, OpCamera>) {
            ++cameraN;
            rememberEntityName(op.uuid);
          } else if constexpr (std::is_same_v<T, OpSky>) {
            ++skyN;
          } else if constexpr (std::is_same_v<T, OpActiveCamera>) {
            ++activeCamN;
          } else if constexpr (std::is_same_v<T, OpCategories>) {
            ++catN;
          } else if constexpr (std::is_same_v<T, OpMaterials>) {
            ++matN;
          } else if constexpr (std::is_same_v<T, OpAnimation>) {
            ++animN;
            if (firstAnimLabel.empty())
              firstAnimLabel = labelForAnimationOp(op);
          }
        },
        opv);
  }

  const int typeCount = (createN > 0) + (destroyN > 0) + (trN > 0) +
                        (nameN > 0) + (parentN > 0) + (meshN > 0) +
                        (lightN > 0) + (cameraN > 0) + (skyN > 0) +
                        (activeCamN > 0) + (catN > 0) + (matN > 0) +
                        (animN > 0);

  auto withEntity = [&](const std::string &base, int count) {
    if (count == 1 && !firstEntityName.empty())
      return base + ": " + firstEntityName;
    if (count > 1)
      return base + " (" + std::to_string(count) + ")";
    return base;
  };

  if (typeCount == 1) {
    if (createN)
      return withEntity("Create Entity", createN);
    if (destroyN)
      return withEntity("Delete Entity", destroyN);
    if (trN)
      return withEntity("Transform", trN);
    if (nameN)
      return withEntity("Rename", nameN);
    if (parentN)
      return withEntity("Reparent", parentN);
    if (meshN)
      return withEntity("Mesh", meshN);
    if (lightN)
      return withEntity("Light", lightN);
    if (cameraN)
      return withEntity("Camera", cameraN);
    if (skyN)
      return "Sky";
    if (activeCamN)
      return "Active Camera";
    if (catN)
      return "Categories";
    if (matN)
      return "Materials";
    if (animN)
      return firstAnimLabel.empty() ? "Animation" : firstAnimLabel;
  }

  if (createN + destroyN > 0)
    return "Hierarchy Edit";
  if (trN > 0 && typeCount <= 2)
    return withEntity("Transform Edit", trN);
  if (animN > 0)
    return firstAnimLabel.empty() ? "Animation + Edit" : firstAnimLabel + " + Edit";
  return "Edit (" + std::to_string((int)entry.ops.size()) + " ops)";
}

PersistedAnimationStateHist
EditorHistory::captureAnimationState(const World &world) const {
  PersistedAnimationStateHist out{};
  if (!m_anim || !m_animClip)
    return out;
  out.valid = true;
  out.name = m_animClip->name;
  out.lastFrame = m_animClip->lastFrame;
  out.loop = m_animClip->loop;
  out.nextBlockId = m_animClip->nextBlockId;
  out.frame = m_anim->frame();
  out.playing = m_anim->playing();
  out.fps = m_anim->fps();

  out.tracks.reserve(m_animClip->tracks.size());
  for (const auto &t : m_animClip->tracks) {
    if (!world.isAlive(t.entity))
      continue;
    const EntityUUID u = world.uuid(t.entity);
    if (!u)
      continue;
    PersistedAnimTrackHist pt{};
    pt.entity = u;
    pt.blockId = t.blockId;
    pt.channel = t.channel;
    pt.curve = t.curve;
    out.tracks.push_back(std::move(pt));
  }

  out.ranges.reserve(m_animClip->entityRanges.size());
  for (const auto &r : m_animClip->entityRanges) {
    if (!world.isAlive(r.entity))
      continue;
    const EntityUUID u = world.uuid(r.entity);
    if (!u)
      continue;
    PersistedAnimRangeHist pr{};
    pr.entity = u;
    pr.blockId = r.blockId;
    pr.start = r.start;
    pr.end = r.end;
    out.ranges.push_back(std::move(pr));
  }

  out.actions.reserve(m_anim->actions().size());
  for (const auto &a : m_anim->actions()) {
    PersistedActionHist pa{};
    pa.name = a.name;
    pa.start = a.start;
    pa.end = a.end;
    pa.tracks.reserve(a.tracks.size());
    for (const auto &t : a.tracks) {
      PersistedActionTrackHist at{};
      at.channel = t.channel;
      at.curve = t.curve;
      pa.tracks.push_back(std::move(at));
    }
    out.actions.push_back(std::move(pa));
  }

  out.strips.reserve(m_anim->strips().size());
  for (const auto &s : m_anim->strips()) {
    PersistedNlaStripHist ps{};
    ps.action = s.action;
    ps.target = world.isAlive(s.target) ? world.uuid(s.target) : EntityUUID{};
    ps.start = s.start;
    ps.end = s.end;
    ps.inFrame = s.inFrame;
    ps.outFrame = s.outFrame;
    ps.timeScale = s.timeScale;
    ps.reverse = s.reverse;
    ps.blend = s.blend;
    ps.influence = s.influence;
    ps.fadeIn = s.fadeIn;
    ps.fadeOut = s.fadeOut;
    ps.layer = s.layer;
    ps.muted = s.muted;
    out.strips.push_back(std::move(ps));
  }

  return out;
}

void EditorHistory::applyAnimationState(const PersistedAnimationStateHist &st,
                                        World &world) {
  if (!m_anim || !m_animClip || !st.valid)
    return;

  auto &clip = *m_animClip;
  clip.name = st.name;
  clip.lastFrame = std::max<AnimFrame>(0, st.lastFrame);
  clip.loop = st.loop;
  clip.nextBlockId = std::max<uint32_t>(1u, st.nextBlockId);
  clip.tracks.clear();
  clip.entityRanges.clear();
  clip.tracks.reserve(st.tracks.size());
  clip.entityRanges.reserve(st.ranges.size());

  for (const auto &t : st.tracks) {
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

  for (const auto &r : st.ranges) {
    if (!r.entity)
      continue;
    EntityID e = world.findByUUID(r.entity);
    if (e == InvalidEntity || !world.isAlive(e))
      continue;
    AnimEntityRange rr{};
    rr.entity = e;
    rr.blockId = r.blockId;
    rr.start = r.start;
    rr.end = std::max<AnimFrame>(r.start, r.end);
    clip.entityRanges.push_back(rr);
  }

  m_anim->clearNla();
  for (const auto &a : st.actions) {
    AnimAction na{};
    na.name = a.name;
    na.start = a.start;
    na.end = a.end;
    na.tracks.reserve(a.tracks.size());
    for (const auto &t : a.tracks) {
      AnimActionTrack at{};
      at.channel = t.channel;
      at.curve = t.curve;
      na.tracks.push_back(std::move(at));
    }
    m_anim->createAction(std::move(na));
  }

  for (const auto &s : st.strips) {
    if (!s.target)
      continue;
    EntityID e = world.findByUUID(s.target);
    if (e == InvalidEntity || !world.isAlive(e))
      continue;
    NlaStrip ns{};
    ns.action = s.action;
    ns.target = e;
    ns.start = s.start;
    ns.end = s.end;
    ns.inFrame = s.inFrame;
    ns.outFrame = s.outFrame;
    ns.timeScale = s.timeScale;
    ns.reverse = s.reverse;
    ns.blend = s.blend;
    ns.influence = s.influence;
    ns.fadeIn = s.fadeIn;
    ns.fadeOut = s.fadeOut;
    ns.layer = s.layer;
    ns.muted = s.muted;
    m_anim->addStrip(ns);
  }

  m_anim->setFps(st.fps);
  m_anim->setFrame(std::clamp<int32_t>(st.frame, 0, clip.lastFrame));
  if (st.playing)
    m_anim->play();
  else
    m_anim->pause();
}

bool EditorHistory::animationStateEqual(const PersistedAnimationStateHist &a,
                                        const PersistedAnimationStateHist &b) const {
  if (a.valid != b.valid)
    return false;
  if (!a.valid)
    return true;
  if (a.name != b.name || a.lastFrame != b.lastFrame || a.loop != b.loop ||
      a.nextBlockId != b.nextBlockId || a.frame != b.frame ||
      a.playing != b.playing || a.fps != b.fps)
    return false;
  if (a.tracks.size() != b.tracks.size() || a.ranges.size() != b.ranges.size() ||
      a.actions.size() != b.actions.size() || a.strips.size() != b.strips.size())
    return false;
  for (size_t i = 0; i < a.tracks.size(); ++i) {
    const auto &x = a.tracks[i];
    const auto &y = b.tracks[i];
    if (x.entity != y.entity || x.blockId != y.blockId || x.channel != y.channel ||
        !animCurvesEqual(x.curve, y.curve))
      return false;
  }
  for (size_t i = 0; i < a.ranges.size(); ++i) {
    const auto &x = a.ranges[i];
    const auto &y = b.ranges[i];
    if (x.entity != y.entity || x.blockId != y.blockId || x.start != y.start ||
        x.end != y.end)
      return false;
  }
  for (size_t i = 0; i < a.actions.size(); ++i) {
    const auto &x = a.actions[i];
    const auto &y = b.actions[i];
    if (x.name != y.name || x.start != y.start || x.end != y.end ||
        x.tracks.size() != y.tracks.size())
      return false;
    for (size_t j = 0; j < x.tracks.size(); ++j) {
      if (x.tracks[j].channel != y.tracks[j].channel ||
          !animCurvesEqual(x.tracks[j].curve, y.tracks[j].curve))
        return false;
    }
  }
  for (size_t i = 0; i < a.strips.size(); ++i) {
    const auto &x = a.strips[i];
    const auto &y = b.strips[i];
    if (x.action != y.action || x.target != y.target || x.start != y.start ||
        x.end != y.end || x.inFrame != y.inFrame || x.outFrame != y.outFrame ||
        x.timeScale != y.timeScale || x.reverse != y.reverse ||
        x.blend != y.blend || x.influence != y.influence ||
        x.fadeIn != y.fadeIn || x.fadeOut != y.fadeOut ||
        x.layer != y.layer || x.muted != y.muted)
      return false;
  }
  return true;
}

static bool isTransformOnly(const HistoryEntry &e) {
  if (e.ops.empty())
    return false;
  for (const auto &opv : e.ops) {
    if (!std::holds_alternative<OpTransform>(opv))
      return false;
  }
  return true;
}

static bool isAnimationOnly(const HistoryEntry &e) {
  if (e.ops.empty())
    return false;
  for (const auto &opv : e.ops) {
    if (!std::holds_alternative<OpAnimation>(opv))
      return false;
  }
  return true;
}

static bool mergeTransformEntry(HistoryEntry &dst,
                                const HistoryEntry &src,
                                double maxDeltaSec) {
  if (!isTransformOnly(dst) || !isTransformOnly(src))
    return false;
  if (src.ops.size() != dst.ops.size())
    return false;
  if ((src.timestampSec - dst.timestampSec) > maxDeltaSec)
    return false;

  std::unordered_map<EntityUUID, CTransform, EntityUUIDHash> srcAfter;
  srcAfter.reserve(src.ops.size());
  for (const auto &opv : src.ops) {
    const auto &op = std::get<OpTransform>(opv);
    srcAfter[op.uuid] = op.after;
  }

  for (auto &opv : dst.ops) {
    auto &op = std::get<OpTransform>(opv);
    auto it = srcAfter.find(op.uuid);
    if (it == srcAfter.end())
      return false;
    op.after = it->second;
  }

  dst.timestampSec = src.timestampSec;
  dst.selection = src.selection;
  return true;
}

static bool mergeAnimationEntry(HistoryEntry &dst,
                                const HistoryEntry &src,
                                double maxDeltaSec) {
  if (!isAnimationOnly(dst) || !isAnimationOnly(src))
    return false;
  if ((src.timestampSec - dst.timestampSec) > maxDeltaSec)
    return false;
  if (dst.ops.size() != 1 || src.ops.size() != 1)
    return false;
  auto &dstOp = std::get<OpAnimation>(dst.ops[0]);
  const auto &srcOp = std::get<OpAnimation>(src.ops[0]);
  dstOp.after = srcOp.after;
  dst.label = src.label;
  dst.timestampSec = src.timestampSec;
  dst.selection = src.selection;
  return true;
}

void EditorHistory::processEvents(const World &world, const WorldEvents &ev,
                                  MaterialSystem &materials,
                                  const Selection &sel) {
  if (!m_recording || m_applying)
    return;

  const CategorySnapshot curCats = captureCategories(world);
  const bool categoriesChanged = !categoriesEqual(curCats, m_lastCategories);

  const bool materialsChanged = (materials.changeSerial() != m_lastMaterialSerial);
  const PersistedAnimationStateHist curAnim = captureAnimationState(world);
  const bool animationChanged = !animationStateEqual(curAnim, m_lastAnimation);

  if (ev.events().empty() && !categoriesChanged && !materialsChanged &&
      !animationChanged)
    return;

  HistoryEntry entry{};
  entry.id = m_nextId++;
  entry.timestampSec = nowSeconds();
  entry.label = labelForEvents(ev, categoriesChanged, materialsChanged);
  entry.selection = captureSelection(world, sel);

  bool sawBatchTransform = false;

  // Build ops from events
  for (const auto &e : ev.events()) {
    switch (e.type) {
    case WorldEventType::EntityCreated: {
      EntityID id = e.a;
      if (!world.isAlive(id))
        break;
      EntitySnapshot snap{};
      const EntityState s = buildState(world, id);
      snap.uuid = s.uuid;
      snap.parent = s.parent;
      snap.name = s.name;
      snap.transform = s.transform;
      snap.hasMesh = s.hasMesh;
      snap.mesh = s.mesh;
      snap.hasCamera = s.hasCamera;
      snap.camera = s.camera;
      snap.cameraMatrices = s.cameraMatrices;
      snap.hasLight = s.hasLight;
      snap.light = s.light;
      snap.hasSky = s.hasSky;
      snap.sky = s.sky;
      snap.categories = s.categories;
      entry.ops.emplace_back(OpEntityCreate{snap});
      break;
    }
    case WorldEventType::EntityDestroyed: {
      auto it = m_cacheById.find(e.a);
      if (it == m_cacheById.end())
        break;
      const EntityState &s = it->second;
      EntitySnapshot snap{};
      snap.uuid = s.uuid;
      snap.parent = s.parent;
      snap.name = s.name;
      snap.transform = s.transform;
      snap.hasMesh = s.hasMesh;
      snap.mesh = s.mesh;
      snap.hasCamera = s.hasCamera;
      snap.camera = s.camera;
      snap.cameraMatrices = s.cameraMatrices;
      snap.hasLight = s.hasLight;
      snap.light = s.light;
      snap.hasSky = s.hasSky;
      snap.sky = s.sky;
      snap.categories = s.categories;
      entry.ops.emplace_back(OpEntityDestroy{snap});
      break;
    }
    case WorldEventType::TransformChanged: {
      EntityID id = e.a;
      if (!world.isAlive(id))
        break;
      EntityUUID u = world.uuid(id);
      auto it = m_cacheById.find(id);
      if (it == m_cacheById.end())
        break;
      if (m_transformBatchActive) {
        if (u) {
          if (m_transformBatchBefore.find(u) == m_transformBatchBefore.end())
            m_transformBatchBefore.emplace(u, it->second.transform);
          m_transformBatchAfter[u] = world.transform(id);
          sawBatchTransform = true;
        }
        break;
      }
      OpTransform op{};
      op.uuid = u;
      op.before = it->second.transform;
      op.after = world.transform(id);
      entry.ops.emplace_back(op);
      break;
    }
    case WorldEventType::NameChanged: {
      EntityID id = e.a;
      if (!world.isAlive(id))
        break;
      EntityUUID u = world.uuid(id);
      auto it = m_cacheById.find(id);
      if (it == m_cacheById.end())
        break;
      OpName op{};
      op.uuid = u;
      op.before = it->second.name.name;
      op.after = world.name(id).name;
      entry.ops.emplace_back(op);
      break;
    }
    case WorldEventType::ParentChanged: {
      EntityID id = e.a;
      if (!world.isAlive(id))
        break;
      EntityUUID u = world.uuid(id);
      auto it = m_cacheById.find(id);
      if (it == m_cacheById.end())
        break;
      OpParent op{};
      op.uuid = u;
      op.before = it->second.parent;
      EntityID np = world.parentOf(id);
      op.after = (np != InvalidEntity) ? world.uuid(np) : EntityUUID{};
      entry.ops.emplace_back(op);
      break;
    }
    case WorldEventType::MeshChanged: {
      EntityID id = e.a;
      if (!world.isAlive(id))
        break;
      EntityUUID u = world.uuid(id);
      auto it = m_cacheById.find(id);
      if (it == m_cacheById.end())
        break;
      OpMesh op{};
      op.uuid = u;
      op.beforeHasMesh = it->second.hasMesh;
      op.before = it->second.mesh;
      op.afterHasMesh = world.hasMesh(id);
      if (op.afterHasMesh)
        op.after = world.mesh(id);
      entry.ops.emplace_back(op);
      break;
    }
    case WorldEventType::LightChanged: {
      EntityID id = e.a;
      if (!world.isAlive(id))
        break;
      EntityUUID u = world.uuid(id);
      auto it = m_cacheById.find(id);
      if (it == m_cacheById.end())
        break;
      OpLight op{};
      op.uuid = u;
      op.beforeHasLight = it->second.hasLight;
      op.before = it->second.light;
      op.afterHasLight = world.hasLight(id);
      if (op.afterHasLight)
        op.after = world.light(id);
      entry.ops.emplace_back(op);
      break;
    }
    case WorldEventType::CameraCreated:
    case WorldEventType::CameraDestroyed: {
      EntityID id = e.a;
      EntityUUID u = world.uuid(id);
      auto it = m_cacheById.find(id);
      OpCamera op{};
      op.uuid = u;
      if (it != m_cacheById.end()) {
        op.beforeHasCamera = it->second.hasCamera;
        op.before = it->second.camera;
        op.beforeMat = it->second.cameraMatrices;
      }
      op.afterHasCamera = world.hasCamera(id);
      if (op.afterHasCamera) {
        op.after = world.camera(id);
        op.afterMat = world.cameraMatrices(id);
      }
      entry.ops.emplace_back(op);
      break;
    }
    case WorldEventType::ActiveCameraChanged: {
      OpActiveCamera op{};
      op.before = (e.b != InvalidEntity) ? world.uuid(e.b) : EntityUUID{};
      op.after = (e.a != InvalidEntity) ? world.uuid(e.a) : EntityUUID{};
      entry.ops.emplace_back(op);
      break;
    }
    case WorldEventType::SkyChanged: {
      OpSky op{};
      op.before = m_lastSky;
      op.after = world.skySettings();
      entry.ops.emplace_back(op);
      m_lastSky = op.after;
      break;
    }
    default:
      break;
    }
  }

  if (categoriesChanged) {
    OpCategories op{};
    op.before = m_lastCategories;
      op.after = curCats;
      entry.ops.emplace_back(op);
      m_lastCategories = op.after;
  }

  if (materialsChanged) {
    OpMaterials op{};
    op.before = m_lastMaterials;
    materials.snapshot(op.after);
    entry.ops.emplace_back(op);
    m_lastMaterials = op.after;
    m_lastMaterialSerial = materials.changeSerial();
  }

  if (animationChanged) {
    OpAnimation op{};
    op.before = m_lastAnimation;
    op.after = curAnim;
    entry.ops.emplace_back(std::move(op));
    m_lastAnimation = curAnim;
  }

  if (entry.ops.empty()) {
    if (sawBatchTransform)
      rebuildCache(world);
    return;
  }

  entry.label = labelForEntry(entry, world);

  if (m_cursor + 1 < (int)m_entries.size())
    m_entries.erase(m_entries.begin() + (m_cursor + 1), m_entries.end());

  if (!m_entries.empty() && m_cursor == (int)m_entries.size() - 1) {
    constexpr double kTransformMergeWindowSec = 0.25;
    constexpr double kAnimationMergeWindowSec = 0.25;
    if (mergeTransformEntry(m_entries.back(), entry,
                            kTransformMergeWindowSec)) {
      rebuildCache(world);
      return;
    }
    if (mergeAnimationEntry(m_entries.back(), entry,
                            kAnimationMergeWindowSec)) {
      rebuildCache(world);
      return;
    }
  }

  m_entries.push_back(std::move(entry));
  m_cursor = (int)m_entries.size() - 1;
  if (m_entries.size() > m_maxEntries) {
    const size_t toDrop = m_entries.size() - m_maxEntries;
    m_entries.erase(m_entries.begin(), m_entries.begin() + (ptrdiff_t)toDrop);
    m_cursor = std::max(-1, m_cursor - (int)toDrop);
  }

  rebuildCache(world);
}

static void applyTransform(World &world, EntityUUID uuid,
                           const CTransform &tr) {
  EntityID e = world.findByUUID(uuid);
  if (e == InvalidEntity)
    return;
  world.transform(e) = tr;
  world.worldTransform(e).dirty = true;
}

bool EditorHistory::undo(World &world, MaterialSystem &materials,
                         Selection &sel) {
  if (!canUndo())
    return false;
  m_applying = true;
  HistoryEntry &entry = m_entries[(size_t)m_cursor];
  for (auto it = entry.ops.rbegin(); it != entry.ops.rend(); ++it) {
    std::visit(
        [&](auto &op) {
          using T = std::decay_t<decltype(op)>;
          if constexpr (std::is_same_v<T, OpEntityCreate>) {
            EntityID e = world.findByUUID(op.snap.uuid);
            if (e != InvalidEntity)
              world.destroyEntity(e);
          } else if constexpr (std::is_same_v<T, OpEntityDestroy>) {
            restoreEntity(world, op.snap);
          } else if constexpr (std::is_same_v<T, OpTransform>) {
            applyTransform(world, op.uuid, op.before);
          } else if constexpr (std::is_same_v<T, OpName>) {
            EntityID e = world.findByUUID(op.uuid);
            if (e != InvalidEntity)
              world.setName(e, op.before);
          } else if constexpr (std::is_same_v<T, OpParent>) {
            EntityID e = world.findByUUID(op.uuid);
            if (e != InvalidEntity) {
              EntityID p =
                  (op.before) ? world.findByUUID(op.before) : InvalidEntity;
              world.setParent(e, p);
            }
          } else if constexpr (std::is_same_v<T, OpMesh>) {
            EntityID e = world.findByUUID(op.uuid);
            if (e != InvalidEntity) {
              if (op.beforeHasMesh)
                world.ensureMesh(e) = op.before;
              else
                world.removeMesh(e);
            }
          } else if constexpr (std::is_same_v<T, OpLight>) {
            EntityID e = world.findByUUID(op.uuid);
            if (e != InvalidEntity) {
              if (op.beforeHasLight)
                world.ensureLight(e) = op.before;
              else
                world.removeLight(e);
            }
          } else if constexpr (std::is_same_v<T, OpCamera>) {
            EntityID e = world.findByUUID(op.uuid);
            if (e != InvalidEntity) {
              if (op.beforeHasCamera) {
                world.ensureCamera(e) = op.before;
                world.cameraMatrices(e) = op.beforeMat;
              } else {
                world.removeCamera(e);
              }
            }
          } else if constexpr (std::is_same_v<T, OpSky>) {
            world.skySettings() = op.before;
          } else if constexpr (std::is_same_v<T, OpActiveCamera>) {
            world.setActiveCameraUUID(op.before);
          } else if constexpr (std::is_same_v<T, OpCategories>) {
            applyCategories(world, op.before);
          } else if constexpr (std::is_same_v<T, OpMaterials>) {
            materials.restore(op.before);
          } else if constexpr (std::is_same_v<T, OpAnimation>) {
            applyAnimationState(op.before, world);
          }
        },
        *it);
  }
  restoreSelection(world, sel, entry.selection);
  m_cursor--;
  world.events().clear();
  m_applying = false;
  rebuildCache(world);
  m_lastCategories = captureCategories(world);
  if (m_materials) {
    m_lastMaterialSerial = m_materials->changeSerial();
    m_materials->snapshot(m_lastMaterials);
  }
  if (m_world && m_anim && m_animClip)
    m_lastAnimation = captureAnimationState(*m_world);
  m_lastSky = world.skySettings();
  return true;
}

bool EditorHistory::redo(World &world, MaterialSystem &materials,
                         Selection &sel) {
  if (!canRedo())
    return false;
  m_applying = true;
  HistoryEntry &entry = m_entries[(size_t)(m_cursor + 1)];
  for (auto &opv : entry.ops) {
    std::visit(
        [&](auto &op) {
          using T = std::decay_t<decltype(op)>;
          if constexpr (std::is_same_v<T, OpEntityCreate>) {
            restoreEntity(world, op.snap);
          } else if constexpr (std::is_same_v<T, OpEntityDestroy>) {
            EntityID e = world.findByUUID(op.snap.uuid);
            if (e != InvalidEntity)
              world.destroyEntity(e);
          } else if constexpr (std::is_same_v<T, OpTransform>) {
            applyTransform(world, op.uuid, op.after);
          } else if constexpr (std::is_same_v<T, OpName>) {
            EntityID e = world.findByUUID(op.uuid);
            if (e != InvalidEntity)
              world.setName(e, op.after);
          } else if constexpr (std::is_same_v<T, OpParent>) {
            EntityID e = world.findByUUID(op.uuid);
            if (e != InvalidEntity) {
              EntityID p =
                  (op.after) ? world.findByUUID(op.after) : InvalidEntity;
              world.setParent(e, p);
            }
          } else if constexpr (std::is_same_v<T, OpMesh>) {
            EntityID e = world.findByUUID(op.uuid);
            if (e != InvalidEntity) {
              if (op.afterHasMesh)
                world.ensureMesh(e) = op.after;
              else
                world.removeMesh(e);
            }
          } else if constexpr (std::is_same_v<T, OpLight>) {
            EntityID e = world.findByUUID(op.uuid);
            if (e != InvalidEntity) {
              if (op.afterHasLight)
                world.ensureLight(e) = op.after;
              else
                world.removeLight(e);
            }
          } else if constexpr (std::is_same_v<T, OpCamera>) {
            EntityID e = world.findByUUID(op.uuid);
            if (e != InvalidEntity) {
              if (op.afterHasCamera) {
                world.ensureCamera(e) = op.after;
                world.cameraMatrices(e) = op.afterMat;
              } else {
                world.removeCamera(e);
              }
            }
          } else if constexpr (std::is_same_v<T, OpSky>) {
            world.skySettings() = op.after;
          } else if constexpr (std::is_same_v<T, OpActiveCamera>) {
            world.setActiveCameraUUID(op.after);
          } else if constexpr (std::is_same_v<T, OpCategories>) {
            applyCategories(world, op.after);
          } else if constexpr (std::is_same_v<T, OpMaterials>) {
            materials.restore(op.after);
          } else if constexpr (std::is_same_v<T, OpAnimation>) {
            applyAnimationState(op.after, world);
          }
        },
        opv);
  }
  restoreSelection(world, sel, entry.selection);
  m_cursor++;
  world.events().clear();
  m_applying = false;
  rebuildCache(world);
  m_lastCategories = captureCategories(world);
  if (m_materials) {
    m_lastMaterialSerial = m_materials->changeSerial();
    m_materials->snapshot(m_lastMaterials);
  }
  if (m_world && m_anim && m_animClip)
    m_lastAnimation = captureAnimationState(*m_world);
  m_lastSky = world.skySettings();
  return true;
}

void EditorHistory::clear() {
  m_entries.clear();
  m_cursor = -1;
  m_nextId = 1;
  m_cacheById.clear();
  if (m_world) {
    rebuildCache(*m_world);
    m_lastCategories = captureCategories(*m_world);
    m_lastSky = m_world->skySettings();
  }
  if (m_materials) {
    m_lastMaterialSerial = m_materials->changeSerial();
    m_materials->snapshot(m_lastMaterials);
  }
  if (m_world && m_anim && m_animClip)
    m_lastAnimation = captureAnimationState(*m_world);
}

void EditorHistory::setMaxEntries(size_t maxEntries) {
  m_maxEntries = std::max<size_t>(1, maxEntries);
  if (m_entries.size() > m_maxEntries) {
    const size_t toDrop = m_entries.size() - m_maxEntries;
    m_entries.erase(m_entries.begin(), m_entries.begin() + (ptrdiff_t)toDrop);
    m_cursor = std::max(-1, m_cursor - (int)toDrop);
  }
}

// ---- Persistence helpers ----
using namespace JsonLite;

static Value jVec3(const glm::vec3 &v) {
  Array a;
  a.emplace_back(v.x);
  a.emplace_back(v.y);
  a.emplace_back(v.z);
  return Value(std::move(a));
}
static Value jVec2(const glm::vec2 &v) {
  Array a;
  a.emplace_back(v.x);
  a.emplace_back(v.y);
  return Value(std::move(a));
}
static Value jVec4(const glm::vec4 &v) {
  Array a;
  a.emplace_back(v.x);
  a.emplace_back(v.y);
  a.emplace_back(v.z);
  a.emplace_back(v.w);
  return Value(std::move(a));
}
static Value jQuatWXYZ(const glm::quat &q) {
  Array a;
  a.emplace_back(q.w);
  a.emplace_back(q.x);
  a.emplace_back(q.y);
  a.emplace_back(q.z);
  return Value(std::move(a));
}
static bool readVec3(const Value &v, glm::vec3 &out) {
  if (!v.isArray())
    return false;
  const auto &a = v.asArray();
  if (a.size() < 3)
    return false;
  out.x = (float)a[0].asNum(out.x);
  out.y = (float)a[1].asNum(out.y);
  out.z = (float)a[2].asNum(out.z);
  return true;
}
static bool readVec2(const Value &v, glm::vec2 &out) {
  if (!v.isArray())
    return false;
  const auto &a = v.asArray();
  if (a.size() < 2)
    return false;
  out.x = (float)a[0].asNum(out.x);
  out.y = (float)a[1].asNum(out.y);
  return true;
}
static bool readVec4(const Value &v, glm::vec4 &out) {
  if (!v.isArray())
    return false;
  const auto &a = v.asArray();
  if (a.size() < 4)
    return false;
  out.x = (float)a[0].asNum(out.x);
  out.y = (float)a[1].asNum(out.y);
  out.z = (float)a[2].asNum(out.z);
  out.w = (float)a[3].asNum(out.w);
  return true;
}
static bool readQuat(const Value &v, glm::quat &out) {
  if (!v.isArray())
    return false;
  const auto &a = v.asArray();
  if (a.size() < 4)
    return false;
  out.w = (float)a[0].asNum(out.w);
  out.x = (float)a[1].asNum(out.x);
  out.y = (float)a[2].asNum(out.y);
  out.z = (float)a[3].asNum(out.z);
  return true;
}

static Value jTransform(const CTransform &t) {
  Object o;
  o["t"] = jVec3(t.translation);
  o["r"] = jQuatWXYZ(t.rotation);
  o["s"] = jVec3(t.scale);
  o["hidden"] = t.hidden;
  o["disabledAnim"] = t.disabledAnim;
  return Value(std::move(o));
}
static void readTransform(const Value &v, CTransform &t) {
  if (!v.isObject())
    return;
  if (const Value *jt = v.get("t"))
    readVec3(*jt, t.translation);
  if (const Value *jr = v.get("r"))
    readQuat(*jr, t.rotation);
  if (const Value *js = v.get("s"))
    readVec3(*js, t.scale);
  if (const Value *jh = v.get("hidden"); jh && jh->isBool())
    t.hidden = jh->asBool(t.hidden);
  if (const Value *jd = v.get("disabledAnim"); jd && jd->isBool())
    t.disabledAnim = jd->asBool(t.disabledAnim);
}

static Value jMesh(const CMesh &m) {
  Object o;
  Array subs;
  subs.reserve(m.submeshes.size());
  for (const auto &sm : m.submeshes) {
    Object js;
    js["name"] = sm.name;
    js["type"] = (double)(int)sm.type;
    Array mh;
    mh.emplace_back((double)sm.material.slot);
    mh.emplace_back((double)sm.material.gen);
    js["material"] = Value(std::move(mh));
    subs.emplace_back(Value(std::move(js)));
  }
  o["submeshes"] = Value(std::move(subs));
  return Value(std::move(o));
}
static void readMesh(const Value &v, CMesh &m) {
  if (!v.isObject())
    return;
  if (const Value *vsubs = v.get("submeshes"); vsubs && vsubs->isArray()) {
    const auto &a = vsubs->asArray();
    m.submeshes.clear();
    m.submeshes.reserve(a.size());
    for (const Value &vs : a) {
      if (!vs.isObject())
        continue;
      MeshSubmesh sm{};
      if (const Value *vn = vs.get("name"); vn && vn->isString())
        sm.name = vn->asString();
      if (const Value *vt = vs.get("type"); vt && vt->isNum())
        sm.type = (ProcMeshType)(int)vt->asNum();
      if (const Value *mh = vs.get("material"); mh && mh->isArray()) {
        const auto &ma = mh->asArray();
        if (ma.size() >= 2) {
          sm.material.slot = (uint32_t)ma[0].asNum();
          sm.material.gen = (uint32_t)ma[1].asNum();
        }
      }
      m.submeshes.push_back(std::move(sm));
    }
  }
}

static Value jCamera(const CCamera &c) {
  Object o;
  o["projection"] = (double)(int)c.projection;
  o["fovYDeg"] = c.fovYDeg;
  o["orthoHeight"] = c.orthoHeight;
  o["nearZ"] = c.nearZ;
  o["farZ"] = c.farZ;
  o["exposure"] = c.exposure;
  o["dirty"] = c.dirty;
  return Value(std::move(o));
}
static void readCamera(const Value &v, CCamera &c) {
  if (!v.isObject())
    return;
  if (const Value *vp = v.get("projection"); vp && vp->isNum())
    c.projection = (CameraProjection)(int)vp->asNum();
  if (const Value *vf = v.get("fovYDeg"); vf && vf->isNum())
    c.fovYDeg = (float)vf->asNum(c.fovYDeg);
  if (const Value *vo = v.get("orthoHeight"); vo && vo->isNum())
    c.orthoHeight = (float)vo->asNum(c.orthoHeight);
  if (const Value *vn = v.get("nearZ"); vn && vn->isNum())
    c.nearZ = (float)vn->asNum(c.nearZ);
  if (const Value *vf = v.get("farZ"); vf && vf->isNum())
    c.farZ = (float)vf->asNum(c.farZ);
  if (const Value *ve = v.get("exposure"); ve && ve->isNum())
    c.exposure = (float)ve->asNum(c.exposure);
  if (const Value *vd = v.get("dirty"); vd && vd->isBool())
    c.dirty = vd->asBool(c.dirty);
}

static Value jCameraMatrices(const CCameraMatrices &m) {
  Object o;
  Array v;
  v.reserve(16);
  const float *p = &m.view[0][0];
  for (int i = 0; i < 16; ++i)
    v.emplace_back(p[i]);
  o["view"] = Value(v);
  v.clear();
  p = &m.proj[0][0];
  for (int i = 0; i < 16; ++i)
    v.emplace_back(p[i]);
  o["proj"] = Value(v);
  v.clear();
  p = &m.viewProj[0][0];
  for (int i = 0; i < 16; ++i)
    v.emplace_back(p[i]);
  o["viewProj"] = Value(v);
  o["dirty"] = m.dirty;
  o["lastW"] = (double)m.lastW;
  o["lastH"] = (double)m.lastH;
  return Value(std::move(o));
}
static void readCameraMatrices(const Value &v, CCameraMatrices &m) {
  if (!v.isObject())
    return;
  auto readMat = [](const Value *arr, glm::mat4 &out) {
    if (!arr || !arr->isArray())
      return;
    const auto &a = arr->asArray();
    if (a.size() < 16)
      return;
    float *p = &out[0][0];
    for (int i = 0; i < 16; ++i)
      p[i] = (float)a[i].asNum(p[i]);
  };
  readMat(v.get("view"), m.view);
  readMat(v.get("proj"), m.proj);
  readMat(v.get("viewProj"), m.viewProj);
  if (const Value *vd = v.get("dirty"); vd && vd->isBool())
    m.dirty = vd->asBool(m.dirty);
  if (const Value *vw = v.get("lastW"); vw && vw->isNum())
    m.lastW = (uint32_t)vw->asNum(m.lastW);
  if (const Value *vh = v.get("lastH"); vh && vh->isNum())
    m.lastH = (uint32_t)vh->asNum(m.lastH);
}

static Value jLight(const CLight &l) {
  Object o;
  o["type"] = (double)(int)l.type;
  o["color"] = jVec3(l.color);
  o["intensity"] = l.intensity;
  o["radius"] = l.radius;
  o["innerAngle"] = l.innerAngle;
  o["outerAngle"] = l.outerAngle;
  o["exposure"] = l.exposure;
  o["enabled"] = l.enabled;
  o["castShadow"] = l.castShadow;
  o["shadowRes"] = (double)l.shadowRes;
  o["cascadeRes"] = (double)l.cascadeRes;
  o["cascadeCount"] = (double)l.cascadeCount;
  o["normalBias"] = l.normalBias;
  o["slopeBias"] = l.slopeBias;
  o["pcfRadius"] = l.pcfRadius;
  o["pointFar"] = l.pointFar;
  return Value(std::move(o));
}
static void readLight(const Value &v, CLight &l) {
  if (!v.isObject())
    return;
  if (const Value *vt = v.get("type"); vt && vt->isNum())
    l.type = (LightType)(int)vt->asNum();
  if (const Value *vc = v.get("color"))
    readVec3(*vc, l.color);
  if (const Value *vi = v.get("intensity"); vi && vi->isNum())
    l.intensity = (float)vi->asNum(l.intensity);
  if (const Value *vr = v.get("radius"); vr && vr->isNum())
    l.radius = (float)vr->asNum(l.radius);
  if (const Value *vi = v.get("innerAngle"); vi && vi->isNum())
    l.innerAngle = (float)vi->asNum(l.innerAngle);
  if (const Value *vo = v.get("outerAngle"); vo && vo->isNum())
    l.outerAngle = (float)vo->asNum(l.outerAngle);
  if (const Value *ve = v.get("exposure"); ve && ve->isNum())
    l.exposure = (float)ve->asNum(l.exposure);
  if (const Value *ve = v.get("enabled"); ve && ve->isBool())
    l.enabled = ve->asBool(l.enabled);
  if (const Value *vs = v.get("castShadow"); vs && vs->isBool())
    l.castShadow = vs->asBool(l.castShadow);
  if (const Value *vsr = v.get("shadowRes"); vsr && vsr->isNum())
    l.shadowRes = (uint16_t)vsr->asNum(l.shadowRes);
  if (const Value *vcr = v.get("cascadeRes"); vcr && vcr->isNum())
    l.cascadeRes = (uint16_t)vcr->asNum(l.cascadeRes);
  if (const Value *vcc = v.get("cascadeCount"); vcc && vcc->isNum())
    l.cascadeCount = (uint8_t)vcc->asNum(l.cascadeCount);
  if (const Value *vnb = v.get("normalBias"); vnb && vnb->isNum())
    l.normalBias = (float)vnb->asNum(l.normalBias);
  if (const Value *vsb = v.get("slopeBias"); vsb && vsb->isNum())
    l.slopeBias = (float)vsb->asNum(l.slopeBias);
  if (const Value *vpf = v.get("pcfRadius"); vpf && vpf->isNum())
    l.pcfRadius = (float)vpf->asNum(l.pcfRadius);
  if (const Value *vpf = v.get("pointFar"); vpf && vpf->isNum())
    l.pointFar = (float)vpf->asNum(l.pointFar);
}

static Value jSky(const CSky &s) {
  Object o;
  o["hdriPath"] = s.hdriPath;
  o["intensity"] = s.intensity;
  o["exposure"] = s.exposure;
  o["rotationYawDeg"] = s.rotationYawDeg;
  o["ambient"] = s.ambient;
  o["enabled"] = s.enabled;
  o["drawBackground"] = s.drawBackground;
  return Value(std::move(o));
}
static void readSky(const Value &v, CSky &s) {
  if (!v.isObject())
    return;
  if (const Value *vp = v.get("hdriPath"); vp && vp->isString())
    s.hdriPath = vp->asString();
  if (const Value *vi = v.get("intensity"); vi && vi->isNum())
    s.intensity = (float)vi->asNum(s.intensity);
  if (const Value *ve = v.get("exposure"); ve && ve->isNum())
    s.exposure = (float)ve->asNum(s.exposure);
  if (const Value *vy = v.get("rotationYawDeg"); vy && vy->isNum())
    s.rotationYawDeg = (float)vy->asNum(s.rotationYawDeg);
  if (const Value *va = v.get("ambient"); va && va->isNum())
    s.ambient = (float)va->asNum(s.ambient);
  if (const Value *ve = v.get("enabled"); ve && ve->isBool())
    s.enabled = ve->asBool(s.enabled);
  if (const Value *vb = v.get("drawBackground"); vb && vb->isBool())
    s.drawBackground = vb->asBool(s.drawBackground);
}

static Value jMaterialData(const MaterialData &m) {
  Object o;
  o["name"] = m.name;
  o["baseColorFactor"] = jVec4(m.baseColorFactor);
  o["emissiveFactor"] = jVec3(m.emissiveFactor);
  o["metallic"] = m.metallic;
  o["roughness"] = m.roughness;
  o["ao"] = m.ao;
  o["uvScale"] = jVec2(m.uvScale);
  o["uvOffset"] = jVec2(m.uvOffset);
  Array tex;
  tex.reserve(m.texPath.size());
  for (const auto &p : m.texPath)
    tex.emplace_back(p);
  o["texPath"] = Value(std::move(tex));
  o["alphaMode"] = (double)(int)m.alphaMode;
  o["alphaCutoff"] = m.alphaCutoff;
  o["tangentSpaceNormal"] = m.tangentSpaceNormal;
  return Value(std::move(o));
}
static void readMaterialData(const Value &v, MaterialData &m) {
  if (!v.isObject())
    return;
  if (const Value *vn = v.get("name"); vn && vn->isString())
    m.name = vn->asString();
  if (const Value *vb = v.get("baseColorFactor"))
    readVec4(*vb, m.baseColorFactor);
  if (const Value *ve = v.get("emissiveFactor"))
    readVec3(*ve, m.emissiveFactor);
  if (const Value *vmc = v.get("metallic"); vmc && vmc->isNum())
    m.metallic = (float)vmc->asNum(m.metallic);
  if (const Value *vr = v.get("roughness"); vr && vr->isNum())
    m.roughness = (float)vr->asNum(m.roughness);
  if (const Value *vao = v.get("ao"); vao && vao->isNum())
    m.ao = (float)vao->asNum(m.ao);
  if (const Value *vus = v.get("uvScale"))
    readVec2(*vus, m.uvScale);
  if (const Value *vuo = v.get("uvOffset"))
    readVec2(*vuo, m.uvOffset);
  if (const Value *vt = v.get("texPath"); vt && vt->isArray()) {
    const auto &ta = vt->asArray();
    const size_t n = std::min(ta.size(), m.texPath.size());
    for (size_t i = 0; i < n; ++i) {
      if (ta[i].isString())
        m.texPath[i] = ta[i].asString();
    }
  }
  if (const Value *vam = v.get("alphaMode"); vam && vam->isNum())
    m.alphaMode = (MatAlphaMode)(int)vam->asNum();
  if (const Value *vac = v.get("alphaCutoff"); vac && vac->isNum())
    m.alphaCutoff = (float)vac->asNum(m.alphaCutoff);
  if (const Value *vtn = v.get("tangentSpaceNormal"); vtn && vtn->isBool())
    m.tangentSpaceNormal = vtn->asBool(m.tangentSpaceNormal);
}

static Value jMaterialGraph(const MaterialGraph &g) {
  Object o;
  o["version"] = 3;
  o["alphaMode"] = (double)(int)g.alphaMode;
  o["alphaCutoff"] = g.alphaCutoff;
  o["nextNodeId"] = (double)g.nextNodeId;
  o["nextLinkId"] = (double)g.nextLinkId;
  Array nodes;
  nodes.reserve(g.nodes.size());
  for (const auto &n : g.nodes) {
    Object jn;
    jn["id"] = (double)n.id;
    jn["type"] = (double)(int)n.type;
    jn["label"] = n.label;
    jn["pos"] = jVec2(n.pos);
    jn["posSet"] = n.posSet;
    jn["f"] = jVec4(n.f);
    Array ju;
    ju.emplace_back((double)n.u.x);
    ju.emplace_back((double)n.u.y);
    ju.emplace_back((double)n.u.z);
    ju.emplace_back((double)n.u.w);
    jn["u"] = Value(std::move(ju));
    jn["path"] = n.path;
    nodes.emplace_back(Value(std::move(jn)));
  }
  o["nodes"] = Value(std::move(nodes));
  Array links;
  links.reserve(g.links.size());
  for (const auto &l : g.links) {
    Object jl;
    jl["id"] = (double)l.id;
    Array from;
    from.emplace_back((double)l.from.node);
    from.emplace_back((double)l.from.slot);
    jl["from"] = Value(std::move(from));
    Array to;
    to.emplace_back((double)l.to.node);
    to.emplace_back((double)l.to.slot);
    jl["to"] = Value(std::move(to));
    links.emplace_back(Value(std::move(jl)));
  }
  o["links"] = Value(std::move(links));
  return Value(std::move(o));
}
static void readMaterialGraph(const Value &v, MaterialGraph &g) {
  if (!v.isObject())
    return;
  if (const Value *va = v.get("alphaMode"); va && va->isNum())
    g.alphaMode = (MatAlphaMode)(int)va->asNum();
  if (const Value *vc = v.get("alphaCutoff"); vc && vc->isNum())
    g.alphaCutoff = (float)vc->asNum(g.alphaCutoff);
  if (const Value *vnid = v.get("nextNodeId"); vnid && vnid->isNum())
    g.nextNodeId = (uint32_t)vnid->asNum(g.nextNodeId);
  if (const Value *vlid = v.get("nextLinkId"); vlid && vlid->isNum())
    g.nextLinkId = (uint64_t)vlid->asNum(g.nextLinkId);
  if (const Value *vNodes = v.get("nodes"); vNodes && vNodes->isArray()) {
    g.nodes.clear();
    for (const Value &vn : vNodes->asArray()) {
      if (!vn.isObject())
        continue;
      MatNode n{};
      if (const Value *vid = vn.get("id"); vid && vid->isNum())
        n.id = (uint32_t)vid->asNum();
      if (const Value *vt = vn.get("type"); vt && vt->isNum())
        n.type = (MatNodeType)(int)vt->asNum();
      if (const Value *vl = vn.get("label"); vl && vl->isString())
        n.label = vl->asString();
      if (const Value *vp = vn.get("pos"))
        readVec2(*vp, n.pos);
      if (const Value *vps = vn.get("posSet"); vps && vps->isBool())
        n.posSet = vps->asBool(n.posSet);
      if (const Value *vf = vn.get("f"))
        readVec4(*vf, n.f);
      if (const Value *vu = vn.get("u"); vu && vu->isArray()) {
        const auto &a = vu->asArray();
        if (a.size() >= 4) {
          n.u.x = (uint32_t)a[0].asNum(n.u.x);
          n.u.y = (uint32_t)a[1].asNum(n.u.y);
          n.u.z = (uint32_t)a[2].asNum(n.u.z);
          n.u.w = (uint32_t)a[3].asNum(n.u.w);
        }
      }
      if (const Value *vp = vn.get("path"); vp && vp->isString())
        n.path = vp->asString();
      g.nodes.push_back(std::move(n));
    }
  }
  if (const Value *vLinks = v.get("links"); vLinks && vLinks->isArray()) {
    g.links.clear();
    for (const Value &vl : vLinks->asArray()) {
      if (!vl.isObject())
        continue;
      MatLink l{};
      if (const Value *vid = vl.get("id"); vid && vid->isNum())
        l.id = (uint64_t)vid->asNum();
      if (const Value *vf = vl.get("from"); vf && vf->isArray()) {
        const auto &a = vf->asArray();
        if (a.size() >= 2) {
          l.from.node = (uint32_t)a[0].asNum();
          l.from.slot = (uint32_t)a[1].asNum();
        }
      }
      if (const Value *vt = vl.get("to"); vt && vt->isArray()) {
        const auto &a = vt->asArray();
        if (a.size() >= 2) {
          l.to.node = (uint32_t)a[0].asNum();
          l.to.slot = (uint32_t)a[1].asNum();
        }
      }
      g.links.push_back(std::move(l));
    }
  }
}

static Value jMaterialSystemSnapshot(const MaterialSystemSnapshot &s) {
  Object o;
  Array slots;
  slots.reserve(s.slots.size());
  for (const auto &ms : s.slots) {
    Object js;
    js["gen"] = (double)ms.gen;
    js["alive"] = ms.alive;
    js["cpu"] = jMaterialData(ms.cpu);
    js["graph"] = jMaterialGraph(ms.graph);
    slots.emplace_back(Value(std::move(js)));
  }
  o["slots"] = Value(std::move(slots));
  Array free;
  free.reserve(s.free.size());
  for (uint32_t f : s.free)
    free.emplace_back((double)f);
  o["free"] = Value(std::move(free));
  o["changeSerial"] = (double)s.changeSerial;
  return Value(std::move(o));
}
static void readMaterialSystemSnapshot(const Value &v,
                                       MaterialSystemSnapshot &s) {
  if (!v.isObject())
    return;
  if (const Value *vs = v.get("slots"); vs && vs->isArray()) {
    s.slots.clear();
    for (const Value &it : vs->asArray()) {
      if (!it.isObject())
        continue;
      MaterialSystem::MaterialSnapshot ms{};
      if (const Value *vg = it.get("gen"); vg && vg->isNum())
        ms.gen = (uint32_t)vg->asNum(ms.gen);
      if (const Value *va = it.get("alive"); va && va->isBool())
        ms.alive = va->asBool(ms.alive);
      if (const Value *vc = it.get("cpu"))
        readMaterialData(*vc, ms.cpu);
      if (const Value *vg = it.get("graph"))
        readMaterialGraph(*vg, ms.graph);
      s.slots.push_back(std::move(ms));
    }
  }
  if (const Value *vf = v.get("free"); vf && vf->isArray()) {
    s.free.clear();
    for (const Value &it : vf->asArray())
      s.free.push_back((uint32_t)it.asNum());
  }
  if (const Value *vc = v.get("changeSerial"); vc && vc->isNum())
    s.changeSerial = (uint64_t)vc->asNum(s.changeSerial);
}

static Value jCategorySnapshot(const CategorySnapshot &s) {
  Object o;
  Array cats;
  cats.reserve(s.categories.size());
  for (const auto &c : s.categories) {
    Object jc;
    jc["name"] = c.name;
    jc["parent"] = (double)c.parent;
    Array ch;
    ch.reserve(c.children.size());
    for (uint32_t v : c.children)
      ch.emplace_back((double)v);
    jc["children"] = Value(std::move(ch));
    Array ents;
    ents.reserve(c.entities.size());
    for (const auto &e : c.entities)
      ents.emplace_back((double)e.index);
    jc["entities"] = Value(std::move(ents));
    cats.emplace_back(Value(std::move(jc)));
  }
  o["categories"] = Value(std::move(cats));
  Object map;
  for (const auto &kv : s.entityCategoriesByUUID) {
    Array arr;
    arr.reserve(kv.second.size());
    for (uint32_t v : kv.second)
      arr.emplace_back((double)v);
    map[std::to_string(kv.first)] = Value(std::move(arr));
  }
  o["entityCategories"] = Value(std::move(map));
  return Value(std::move(o));
}
static void readCategorySnapshot(const Value &v, CategorySnapshot &s) {
  if (!v.isObject())
    return;
  if (const Value *vc = v.get("categories"); vc && vc->isArray()) {
    s.categories.clear();
    for (const Value &it : vc->asArray()) {
      if (!it.isObject())
        continue;
      World::Category c{};
      if (const Value *vn = it.get("name"); vn && vn->isString())
        c.name = vn->asString();
      if (const Value *vp = it.get("parent"); vp && vp->isNum())
        c.parent = (int32_t)vp->asNum(c.parent);
      if (const Value *vch = it.get("children"); vch && vch->isArray()) {
        for (const Value &ch : vch->asArray())
          c.children.push_back((uint32_t)ch.asNum());
      }
      if (const Value *ve = it.get("entities"); ve && ve->isArray()) {
        for (const Value &ch : ve->asArray()) {
          EntityID e{};
          e.index = (uint32_t)ch.asNum();
          e.generation = 1;
          c.entities.push_back(e);
        }
      }
      s.categories.push_back(std::move(c));
    }
  }
  if (const Value *vm = v.get("entityCategories"); vm && vm->isObject()) {
    s.entityCategoriesByUUID.clear();
    for (const auto &kv : vm->asObject()) {
      const uint64_t uuid = std::stoull(kv.first);
      std::vector<uint32_t> cats;
      if (kv.second.isArray()) {
        for (const Value &it : kv.second.asArray())
          cats.push_back((uint32_t)it.asNum());
      }
      s.entityCategoriesByUUID.emplace(uuid, std::move(cats));
    }
  }
}

static Value jEntitySnapshot(const EntitySnapshot &s) {
  Object o;
  o["uuid"] = (double)s.uuid.value;
  o["parent"] = s.parent ? Value((double)s.parent.value) : Value(nullptr);
  o["name"] = s.name.name;
  o["transform"] = jTransform(s.transform);
  o["hasMesh"] = s.hasMesh;
  if (s.hasMesh)
    o["mesh"] = jMesh(s.mesh);
  o["hasCamera"] = s.hasCamera;
  if (s.hasCamera) {
    o["camera"] = jCamera(s.camera);
    o["cameraMatrices"] = jCameraMatrices(s.cameraMatrices);
  }
  o["hasLight"] = s.hasLight;
  if (s.hasLight)
    o["light"] = jLight(s.light);
  o["hasSky"] = s.hasSky;
  if (s.hasSky)
    o["sky"] = jSky(s.sky);
  Array cats;
  cats.reserve(s.categories.size());
  for (uint32_t c : s.categories)
    cats.emplace_back((double)c);
  o["categories"] = Value(std::move(cats));
  return Value(std::move(o));
}
static void readEntitySnapshot(const Value &v, EntitySnapshot &s) {
  if (!v.isObject())
    return;
  if (const Value *vu = v.get("uuid"); vu && vu->isNum())
    s.uuid = EntityUUID{(uint64_t)vu->asNum()};
  if (const Value *vp = v.get("parent"); vp && vp->isNum())
    s.parent = EntityUUID{(uint64_t)vp->asNum()};
  if (const Value *vn = v.get("name"); vn && vn->isString())
    s.name.name = vn->asString();
  if (const Value *vt = v.get("transform"))
    readTransform(*vt, s.transform);
  if (const Value *vhm = v.get("hasMesh"); vhm && vhm->isBool())
    s.hasMesh = vhm->asBool(s.hasMesh);
  if (s.hasMesh && v.get("mesh"))
    readMesh(*v.get("mesh"), s.mesh);
  if (const Value *vhc = v.get("hasCamera"); vhc && vhc->isBool())
    s.hasCamera = vhc->asBool(s.hasCamera);
  if (s.hasCamera && v.get("camera"))
    readCamera(*v.get("camera"), s.camera);
  if (s.hasCamera && v.get("cameraMatrices"))
    readCameraMatrices(*v.get("cameraMatrices"), s.cameraMatrices);
  if (const Value *vhl = v.get("hasLight"); vhl && vhl->isBool())
    s.hasLight = vhl->asBool(s.hasLight);
  if (s.hasLight && v.get("light"))
    readLight(*v.get("light"), s.light);
  if (const Value *vhs = v.get("hasSky"); vhs && vhs->isBool())
    s.hasSky = vhs->asBool(s.hasSky);
  if (s.hasSky && v.get("sky"))
    readSky(*v.get("sky"), s.sky);
  if (const Value *vc = v.get("categories"); vc && vc->isArray()) {
    for (const Value &it : vc->asArray())
      s.categories.push_back((uint32_t)it.asNum());
  }
}

static Value jSelection(const HistorySelectionSnapshot &s) {
  Object o;
  o["kind"] = (double)(int)s.kind;
  o["activeMaterial"] = Value((double)((uint64_t)s.activeMaterial.slot << 32 |
                                       (uint64_t)s.activeMaterial.gen));
  Array picks;
  for (const auto &p : s.picks) {
    Object jp;
    jp["uuid"] = (double)p.first.value;
    jp["sub"] = (double)p.second;
    picks.emplace_back(Value(std::move(jp)));
  }
  o["picks"] = Value(std::move(picks));
  if (s.activePick.first)
    o["activePick"] = Value((double)s.activePick.first.value);
  if (s.activeEntity)
    o["activeEntity"] = Value((double)s.activeEntity.value);
  return Value(std::move(o));
}
static void readSelection(const Value &v, HistorySelectionSnapshot &s) {
  if (!v.isObject())
    return;
  if (const Value *vk = v.get("kind"); vk && vk->isNum())
    s.kind = (SelectionKind)(int)vk->asNum();
  if (const Value *vam = v.get("activeMaterial"); vam && vam->isNum()) {
    const uint64_t packed = (uint64_t)vam->asNum();
    s.activeMaterial.slot = (uint32_t)(packed >> 32);
    s.activeMaterial.gen = (uint32_t)(packed & 0xffffffffu);
  }
  if (const Value *vp = v.get("picks"); vp && vp->isArray()) {
    for (const Value &it : vp->asArray()) {
      if (!it.isObject())
        continue;
      EntityUUID u{};
      uint32_t sub = 0;
      if (const Value *vu = it.get("uuid"); vu && vu->isNum())
        u = EntityUUID{(uint64_t)vu->asNum()};
      if (const Value *vs = it.get("sub"); vs && vs->isNum())
        sub = (uint32_t)vs->asNum();
      if (u)
        s.picks.emplace_back(u, sub);
    }
  }
  if (const Value *va = v.get("activePick"); va && va->isNum())
    s.activePick = {EntityUUID{(uint64_t)va->asNum()}, 0};
  if (const Value *ve = v.get("activeEntity"); ve && ve->isNum())
    s.activeEntity = EntityUUID{(uint64_t)ve->asNum()};
}

static Value jHistoryOp(const HistoryOp &op) {
  Object o;
  std::visit(
      [&](auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, OpEntityCreate>) {
          o["type"] = "EntityCreate";
          o["snap"] = jEntitySnapshot(v.snap);
        } else if constexpr (std::is_same_v<T, OpEntityDestroy>) {
          o["type"] = "EntityDestroy";
          o["snap"] = jEntitySnapshot(v.snap);
        } else if constexpr (std::is_same_v<T, OpTransform>) {
          o["type"] = "Transform";
          o["uuid"] = (double)v.uuid.value;
          o["before"] = jTransform(v.before);
          o["after"] = jTransform(v.after);
        } else if constexpr (std::is_same_v<T, OpName>) {
          o["type"] = "Name";
          o["uuid"] = (double)v.uuid.value;
          o["before"] = v.before;
          o["after"] = v.after;
        } else if constexpr (std::is_same_v<T, OpParent>) {
          o["type"] = "Parent";
          o["uuid"] = (double)v.uuid.value;
          o["before"] = v.before ? Value((double)v.before.value) : Value(nullptr);
          o["after"] = v.after ? Value((double)v.after.value) : Value(nullptr);
        } else if constexpr (std::is_same_v<T, OpMesh>) {
          o["type"] = "Mesh";
          o["uuid"] = (double)v.uuid.value;
          o["beforeHas"] = v.beforeHasMesh;
          o["afterHas"] = v.afterHasMesh;
          if (v.beforeHasMesh)
            o["before"] = jMesh(v.before);
          if (v.afterHasMesh)
            o["after"] = jMesh(v.after);
        } else if constexpr (std::is_same_v<T, OpLight>) {
          o["type"] = "Light";
          o["uuid"] = (double)v.uuid.value;
          o["beforeHas"] = v.beforeHasLight;
          o["afterHas"] = v.afterHasLight;
          if (v.beforeHasLight)
            o["before"] = jLight(v.before);
          if (v.afterHasLight)
            o["after"] = jLight(v.after);
        } else if constexpr (std::is_same_v<T, OpCamera>) {
          o["type"] = "Camera";
          o["uuid"] = (double)v.uuid.value;
          o["beforeHas"] = v.beforeHasCamera;
          o["afterHas"] = v.afterHasCamera;
          if (v.beforeHasCamera) {
            o["before"] = jCamera(v.before);
            o["beforeMat"] = jCameraMatrices(v.beforeMat);
          }
          if (v.afterHasCamera) {
            o["after"] = jCamera(v.after);
            o["afterMat"] = jCameraMatrices(v.afterMat);
          }
        } else if constexpr (std::is_same_v<T, OpSky>) {
          o["type"] = "Sky";
          o["before"] = jSky(v.before);
          o["after"] = jSky(v.after);
        } else if constexpr (std::is_same_v<T, OpActiveCamera>) {
          o["type"] = "ActiveCamera";
          o["before"] = v.before ? Value((double)v.before.value) : Value(nullptr);
          o["after"] = v.after ? Value((double)v.after.value) : Value(nullptr);
        } else if constexpr (std::is_same_v<T, OpCategories>) {
          o["type"] = "Categories";
          o["before"] = jCategorySnapshot(v.before);
          o["after"] = jCategorySnapshot(v.after);
        } else if constexpr (std::is_same_v<T, OpMaterials>) {
          o["type"] = "Materials";
          o["before"] = jMaterialSystemSnapshot(v.before);
          o["after"] = jMaterialSystemSnapshot(v.after);
        }
      },
      op);
  return Value(std::move(o));
}

static bool readHistoryOp(const Value &v, HistoryOp &out) {
  if (!v.isObject())
    return false;
  const Value *vt = v.get("type");
  if (!vt || !vt->isString())
    return false;
  const std::string &t = vt->asString();
  if (t == "EntityCreate" || t == "EntityDestroy") {
    EntitySnapshot s{};
    if (const Value *vs = v.get("snap"))
      readEntitySnapshot(*vs, s);
    if (t == "EntityCreate")
      out = OpEntityCreate{s};
    else
      out = OpEntityDestroy{s};
    return true;
  }
  if (t == "Transform") {
    OpTransform op{};
    if (const Value *vu = v.get("uuid"); vu && vu->isNum())
      op.uuid = EntityUUID{(uint64_t)vu->asNum()};
    if (const Value *vb = v.get("before"))
      readTransform(*vb, op.before);
    if (const Value *va = v.get("after"))
      readTransform(*va, op.after);
    out = op;
    return true;
  }
  if (t == "Name") {
    OpName op{};
    if (const Value *vu = v.get("uuid"); vu && vu->isNum())
      op.uuid = EntityUUID{(uint64_t)vu->asNum()};
    if (const Value *vb = v.get("before"); vb && vb->isString())
      op.before = vb->asString();
    if (const Value *va = v.get("after"); va && va->isString())
      op.after = va->asString();
    out = op;
    return true;
  }
  if (t == "Parent") {
    OpParent op{};
    if (const Value *vu = v.get("uuid"); vu && vu->isNum())
      op.uuid = EntityUUID{(uint64_t)vu->asNum()};
    if (const Value *vb = v.get("before"); vb && vb->isNum())
      op.before = EntityUUID{(uint64_t)vb->asNum()};
    if (const Value *va = v.get("after"); va && va->isNum())
      op.after = EntityUUID{(uint64_t)va->asNum()};
    out = op;
    return true;
  }
  if (t == "Mesh") {
    OpMesh op{};
    if (const Value *vu = v.get("uuid"); vu && vu->isNum())
      op.uuid = EntityUUID{(uint64_t)vu->asNum()};
    if (const Value *vb = v.get("beforeHas"); vb && vb->isBool())
      op.beforeHasMesh = vb->asBool(op.beforeHasMesh);
    if (const Value *va = v.get("afterHas"); va && va->isBool())
      op.afterHasMesh = va->asBool(op.afterHasMesh);
    if (const Value *vb = v.get("before"))
      readMesh(*vb, op.before);
    if (const Value *va = v.get("after"))
      readMesh(*va, op.after);
    out = op;
    return true;
  }
  if (t == "Light") {
    OpLight op{};
    if (const Value *vu = v.get("uuid"); vu && vu->isNum())
      op.uuid = EntityUUID{(uint64_t)vu->asNum()};
    if (const Value *vb = v.get("beforeHas"); vb && vb->isBool())
      op.beforeHasLight = vb->asBool(op.beforeHasLight);
    if (const Value *va = v.get("afterHas"); va && va->isBool())
      op.afterHasLight = va->asBool(op.afterHasLight);
    if (const Value *vb = v.get("before"))
      readLight(*vb, op.before);
    if (const Value *va = v.get("after"))
      readLight(*va, op.after);
    out = op;
    return true;
  }
  if (t == "Camera") {
    OpCamera op{};
    if (const Value *vu = v.get("uuid"); vu && vu->isNum())
      op.uuid = EntityUUID{(uint64_t)vu->asNum()};
    if (const Value *vb = v.get("beforeHas"); vb && vb->isBool())
      op.beforeHasCamera = vb->asBool(op.beforeHasCamera);
    if (const Value *va = v.get("afterHas"); va && va->isBool())
      op.afterHasCamera = va->asBool(op.afterHasCamera);
    if (const Value *vb = v.get("before"))
      readCamera(*vb, op.before);
    if (const Value *va = v.get("after"))
      readCamera(*va, op.after);
    if (const Value *vb = v.get("beforeMat"))
      readCameraMatrices(*vb, op.beforeMat);
    if (const Value *va = v.get("afterMat"))
      readCameraMatrices(*va, op.afterMat);
    out = op;
    return true;
  }
  if (t == "Sky") {
    OpSky op{};
    if (const Value *vb = v.get("before"))
      readSky(*vb, op.before);
    if (const Value *va = v.get("after"))
      readSky(*va, op.after);
    out = op;
    return true;
  }
  if (t == "ActiveCamera") {
    OpActiveCamera op{};
    if (const Value *vb = v.get("before"); vb && vb->isNum())
      op.before = EntityUUID{(uint64_t)vb->asNum()};
    if (const Value *va = v.get("after"); va && va->isNum())
      op.after = EntityUUID{(uint64_t)va->asNum()};
    out = op;
    return true;
  }
  if (t == "Categories") {
    OpCategories op{};
    if (const Value *vb = v.get("before"))
      readCategorySnapshot(*vb, op.before);
    if (const Value *va = v.get("after"))
      readCategorySnapshot(*va, op.after);
    out = op;
    return true;
  }
  if (t == "Materials") {
    OpMaterials op{};
    if (const Value *vb = v.get("before"))
      readMaterialSystemSnapshot(*vb, op.before);
    if (const Value *va = v.get("after"))
      readMaterialSystemSnapshot(*va, op.after);
    out = op;
    return true;
  }
  return false;
}

bool EditorHistory::saveToFile(const std::string &path) const {
  Object root;
  root["type"] = "NyxHistory";
  root["version"] = 1;
  root["cursor"] = (double)m_cursor;
  root["nextId"] = (double)m_nextId;
  root["maxEntries"] = (double)m_maxEntries;
  Array ents;
  ents.reserve(m_entries.size());
  for (const auto &e : m_entries) {
    Object je;
    je["id"] = (double)e.id;
    je["label"] = e.label;
    je["time"] = e.timestampSec;
    je["selection"] = jSelection(e.selection);
    Array ops;
    ops.reserve(e.ops.size());
    for (const auto &op : e.ops)
      ops.emplace_back(jHistoryOp(op));
    je["ops"] = Value(std::move(ops));
    ents.emplace_back(Value(std::move(je)));
  }
  root["entries"] = Value(std::move(ents));
  const std::string out = stringify(Value(std::move(root)), true, 2);
  std::filesystem::path p(path);
  std::error_code ec;
  std::filesystem::create_directories(p.parent_path(), ec);
  std::ofstream f(path, std::ios::binary);
  if (!f.is_open())
    return false;
  f.write(out.data(), (std::streamsize)out.size());
  return true;
}

bool EditorHistory::loadFromFile(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open())
    return false;
  std::string text((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
  Value root;
  JsonLite::ParseError err{};
  if (!parse(text, root, err))
    return false;
  if (!root.isObject())
    return false;
  if (const Value *vt = root.get("type"); !vt || !vt->isString() ||
                                           vt->asString() != "NyxHistory")
    return false;
  if (const Value *vc = root.get("cursor"); vc && vc->isNum())
    m_cursor = (int)vc->asNum(m_cursor);
  if (const Value *vn = root.get("nextId"); vn && vn->isNum())
    m_nextId = (uint64_t)vn->asNum(m_nextId);
  if (const Value *vm = root.get("maxEntries"); vm && vm->isNum())
    m_maxEntries = (size_t)vm->asNum(m_maxEntries);
  if (const Value *ve = root.get("entries"); ve && ve->isArray()) {
    m_entries.clear();
    for (const Value &it : ve->asArray()) {
      if (!it.isObject())
        continue;
      HistoryEntry e{};
      if (const Value *vid = it.get("id"); vid && vid->isNum())
        e.id = (uint64_t)vid->asNum();
      if (const Value *vl = it.get("label"); vl && vl->isString())
        e.label = vl->asString();
      if (const Value *vt = it.get("time"); vt && vt->isNum())
        e.timestampSec = vt->asNum();
      if (const Value *vs = it.get("selection"))
        readSelection(*vs, e.selection);
      if (const Value *vo = it.get("ops"); vo && vo->isArray()) {
        for (const Value &opv : vo->asArray()) {
          HistoryOp op;
          if (readHistoryOp(opv, op))
            e.ops.emplace_back(std::move(op));
        }
      }
      m_entries.push_back(std::move(e));
    }
  }
  if (m_entries.size() > m_maxEntries) {
    const size_t toDrop = m_entries.size() - m_maxEntries;
    m_entries.erase(m_entries.begin(), m_entries.begin() + (ptrdiff_t)toDrop);
    m_cursor = std::max(-1, m_cursor - (int)toDrop);
  }
  return true;
}

} // namespace Nyx
