#include "EditorHistory.h"

#include "scene/World.h"

#include <variant>

namespace Nyx {

namespace {

static void applyTransform(World &world, EntityUUID uuid,
                           const CTransform &tr) {
  EntityID e = world.findByUUID(uuid);
  if (e == InvalidEntity)
    return;
  world.transform(e) = tr;
  world.worldTransform(e).dirty = true;
}

} // namespace

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
  ++m_revision;
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
  ++m_revision;
  return true;
}


} // namespace Nyx
