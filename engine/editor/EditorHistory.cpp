#include "EditorHistory.h"

#include "scene/Pick.h"

#include <algorithm>

namespace Nyx {

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

void EditorHistory::restoreSelection(
    const World &world, Selection &sel,
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
  for (int i = (int)world.categories().size() - 1; i >= 0; --i)
    world.removeCategory((uint32_t)i);

  std::vector<uint32_t> oldToNew;
  oldToNew.reserve(snap.categories.size());
  for (const auto &c : snap.categories) {
    uint32_t idx = world.addCategory(c.name);
    oldToNew.push_back(idx);
  }

  for (size_t i = 0; i < snap.categories.size(); ++i) {
    int32_t p = snap.categories[i].parent;
    if (p >= 0 && p < (int32_t)oldToNew.size()) {
      world.setCategoryParent(oldToNew[i], (int32_t)oldToNew[(size_t)p]);
    }
  }

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

  if (snap.parent) {
    EntityID p = world.findByUUID(snap.parent);
    if (p != InvalidEntity)
      world.setParent(e, p);
  }

  world.transform(e) = snap.transform;
  world.worldTransform(e).dirty = true;
  if (snap.hasMesh)
    world.ensureMesh(e) = snap.mesh;
  if (snap.hasCamera) {
    world.ensureCamera(e) = snap.camera;
    world.cameraMatrices(e) = snap.cameraMatrices;
  }
  if (snap.hasLight)
    world.ensureLight(e) = snap.light;
  if (snap.hasSky)
    world.ensureSky(e) = snap.sky;
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
    return firstAnimLabel.empty() ? "Animation + Edit"
                                  : firstAnimLabel + " + Edit";
  return "Edit (" + std::to_string((int)entry.ops.size()) + " ops)";
}

} // namespace Nyx
