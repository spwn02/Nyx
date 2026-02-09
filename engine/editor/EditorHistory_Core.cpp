#include "EditorHistory.h"

#include "animation/AnimationSystem.h"
#include "scene/World.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <variant>

namespace Nyx {

static double nowSeconds() {
  using clock = std::chrono::steady_clock;
  static const auto start = clock::now();
  const auto t = clock::now();
  return std::chrono::duration<double>(t - start).count();
}

static bool vecEq(const std::vector<uint32_t> &a,
                  const std::vector<uint32_t> &b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
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

static bool mergeTransformEntry(HistoryEntry &dst, const HistoryEntry &src,
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

static bool mergeAnimationEntry(HistoryEntry &dst, const HistoryEntry &src,
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
  ++m_revision;
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
  ++m_revision;
  rebuildCache(world);
}

void EditorHistory::processEvents(const World &world, const WorldEvents &ev,
                                  MaterialSystem &materials,
                                  const Selection &sel) {
  if (!m_recording || m_applying)
    return;

  bool sawCategoryEvent = false;
  for (const auto &we : ev.events()) {
    if (we.type == WorldEventType::CategoriesChanged) {
      sawCategoryEvent = true;
      break;
    }
  }

  CategorySnapshot curCats{};
  bool categoriesChanged = false;
  if (sawCategoryEvent) {
    curCats = captureCategories(world);
    categoriesChanged = !categoriesEqual(curCats, m_lastCategories);
  }

  const bool materialsChanged =
      (materials.changeSerial() != m_lastMaterialSerial);
  const PersistedAnimationStateHist curAnim = captureAnimationState(world);
  const bool animationChanged = !animationStateEqual(curAnim, m_lastAnimation);

  if (m_absorbMaterialOnlyChanges && ev.events().empty() && !categoriesChanged &&
      !animationChanged && materialsChanged) {
    materials.snapshot(m_lastMaterials);
    m_lastMaterialSerial = materials.changeSerial();
    return;
  }

  if (ev.events().empty() && !categoriesChanged && !materialsChanged &&
      !animationChanged)
    return;

  HistoryEntry entry{};
  entry.id = m_nextId++;
  entry.timestampSec = nowSeconds();
  entry.label = labelForEvents(ev, categoriesChanged, materialsChanged);
  entry.selection = captureSelection(world, sel);
  entry.ops.reserve(ev.events().size() + 3);

  bool sawBatchTransform = false;

  struct EventEntityContext final {
    bool aliveLoaded = false;
    bool alive = false;
    bool uuidLoaded = false;
    EntityUUID uuid{};
    bool cachedLoaded = false;
    const EntityState *cached = nullptr;
  };
  std::unordered_map<EntityID, EventEntityContext, EntityHash> eventEntityCtx;
  eventEntityCtx.reserve(ev.events().size());

  auto ctxFor = [&](EntityID id) -> EventEntityContext & {
    return eventEntityCtx.try_emplace(id).first->second;
  };
  auto isAliveCached = [&](EntityID id) -> bool {
    auto &ctx = ctxFor(id);
    if (!ctx.aliveLoaded) {
      ctx.alive = world.isAlive(id);
      ctx.aliveLoaded = true;
    }
    return ctx.alive;
  };
  auto uuidCached = [&](EntityID id) -> EntityUUID {
    auto &ctx = ctxFor(id);
    if (!ctx.uuidLoaded) {
      ctx.uuid = world.uuid(id);
      ctx.uuidLoaded = true;
    }
    return ctx.uuid;
  };
  auto stateCached = [&](EntityID id) -> const EntityState * {
    auto &ctx = ctxFor(id);
    if (!ctx.cachedLoaded) {
      auto it = m_cacheById.find(id);
      ctx.cached = (it != m_cacheById.end()) ? &it->second : nullptr;
      ctx.cachedLoaded = true;
    }
    return ctx.cached;
  };

  for (const auto &e : ev.events()) {
    switch (e.type) {
    case WorldEventType::EntityCreated: {
      EntityID id = e.a;
      if (!isAliveCached(id))
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
      const EntityState *s = stateCached(e.a);
      if (!s)
        break;
      EntitySnapshot snap{};
      snap.uuid = s->uuid;
      snap.parent = s->parent;
      snap.name = s->name;
      snap.transform = s->transform;
      snap.hasMesh = s->hasMesh;
      snap.mesh = s->mesh;
      snap.hasCamera = s->hasCamera;
      snap.camera = s->camera;
      snap.cameraMatrices = s->cameraMatrices;
      snap.hasLight = s->hasLight;
      snap.light = s->light;
      snap.hasSky = s->hasSky;
      snap.sky = s->sky;
      snap.categories = s->categories;
      entry.ops.emplace_back(OpEntityDestroy{snap});
      break;
    }
    case WorldEventType::TransformChanged: {
      EntityID id = e.a;
      if (!isAliveCached(id))
        break;
      EntityUUID u = uuidCached(id);
      const EntityState *s = stateCached(id);
      if (!s)
        break;
      if (m_transformBatchActive) {
        if (u) {
          if (m_transformBatchBefore.find(u) == m_transformBatchBefore.end())
            m_transformBatchBefore.emplace(u, s->transform);
          m_transformBatchAfter[u] = world.transform(id);
          sawBatchTransform = true;
        }
        break;
      }
      OpTransform op{};
      op.uuid = u;
      op.before = s->transform;
      op.after = world.transform(id);
      entry.ops.emplace_back(op);
      break;
    }
    case WorldEventType::NameChanged: {
      EntityID id = e.a;
      if (!isAliveCached(id))
        break;
      EntityUUID u = uuidCached(id);
      const EntityState *s = stateCached(id);
      if (!s)
        break;
      OpName op{};
      op.uuid = u;
      op.before = s->name.name;
      op.after = world.name(id).name;
      entry.ops.emplace_back(op);
      break;
    }
    case WorldEventType::ParentChanged: {
      EntityID id = e.a;
      if (!isAliveCached(id))
        break;
      EntityUUID u = uuidCached(id);
      const EntityState *s = stateCached(id);
      if (!s)
        break;
      OpParent op{};
      op.uuid = u;
      op.before = s->parent;
      EntityID np = world.parentOf(id);
      op.after = (np != InvalidEntity) ? world.uuid(np) : EntityUUID{};
      entry.ops.emplace_back(op);
      break;
    }
    case WorldEventType::MeshChanged: {
      EntityID id = e.a;
      if (!isAliveCached(id))
        break;
      EntityUUID u = uuidCached(id);
      const EntityState *s = stateCached(id);
      if (!s)
        break;
      OpMesh op{};
      op.uuid = u;
      op.beforeHasMesh = s->hasMesh;
      op.before = s->mesh;
      op.afterHasMesh = world.hasMesh(id);
      if (op.afterHasMesh)
        op.after = world.mesh(id);
      entry.ops.emplace_back(op);
      break;
    }
    case WorldEventType::LightChanged: {
      EntityID id = e.a;
      if (!isAliveCached(id))
        break;
      EntityUUID u = uuidCached(id);
      const EntityState *s = stateCached(id);
      if (!s)
        break;
      OpLight op{};
      op.uuid = u;
      op.beforeHasLight = s->hasLight;
      op.before = s->light;
      op.afterHasLight = world.hasLight(id);
      if (op.afterHasLight)
        op.after = world.light(id);
      entry.ops.emplace_back(op);
      break;
    }
    case WorldEventType::CameraCreated:
    case WorldEventType::CameraDestroyed: {
      EntityID id = e.a;
      EntityUUID u = uuidCached(id);
      const EntityState *s = stateCached(id);
      OpCamera op{};
      op.uuid = u;
      if (s) {
        op.beforeHasCamera = s->hasCamera;
        op.before = s->camera;
        op.beforeMat = s->cameraMatrices;
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
    if (mergeTransformEntry(m_entries.back(), entry, kTransformMergeWindowSec)) {
      ++m_revision;
      rebuildCache(world);
      return;
    }
    if (mergeAnimationEntry(m_entries.back(), entry, kAnimationMergeWindowSec)) {
      ++m_revision;
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

  ++m_revision;
  rebuildCache(world);
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
  ++m_revision;
}

void EditorHistory::setMaxEntries(size_t maxEntries) {
  m_maxEntries = std::max<size_t>(1, maxEntries);
  if (m_entries.size() > m_maxEntries) {
    const size_t toDrop = m_entries.size() - m_maxEntries;
    m_entries.erase(m_entries.begin(), m_entries.begin() + (ptrdiff_t)toDrop);
    m_cursor = std::max(-1, m_cursor - (int)toDrop);
  }
}

} // namespace Nyx
