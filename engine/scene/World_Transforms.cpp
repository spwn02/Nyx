#include "World.h"

#include "render/material/MaterialSystem.h"
#include "scene/material/MaterialData.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Nyx {

glm::mat4 World::localMatrix(EntityID e) const {
  const auto &tr = m_tr.at(e);
  glm::mat4 M(1.0f);
  M = glm::translate(M, tr.translation);
  M *= glm::toMat4(tr.rotation);
  M = glm::scale(M, tr.scale);
  return M;
}

void World::markWorldDirtyRecursive(EntityID e) {
  if (!isAlive(e))
    return;

  m_wtr.at(e).dirty = true;

  EntityID ch = m_hier.at(e).firstChild;
  while (ch != InvalidEntity) {
    EntityID next = m_hier.at(ch).nextSibling;
    markWorldDirtyRecursive(ch);
    ch = next;
  }
}

void World::setParentKeepWorld(EntityID child, EntityID newParent) {
  if (!isAlive(child))
    return;

  if (newParent != InvalidEntity) {
    if (!isAlive(newParent))
      return;
    if (child == newParent)
      return;

    EntityID p = parentOf(newParent);
    while (p != InvalidEntity) {
      if (p == child)
        return;
      p = parentOf(p);
    }
  }

  updateTransforms();

  const EntityID oldParent = parentOf(child);
  const glm::mat4 oldWorld = m_wtr.at(child).world;

  detachFromParent(child);
  attachToParent(child, newParent);

  glm::mat4 parentWorld(1.0f);
  if (newParent != InvalidEntity) {
    parentWorld = m_wtr.at(newParent).world;
  }

  const glm::mat4 newLocal = glm::inverse(parentWorld) * oldWorld;

  glm::vec3 skew;
  glm::vec4 persp;
  glm::vec3 scale;
  glm::quat rot;
  glm::vec3 trans;
  glm::decompose(newLocal, scale, rot, trans, skew, persp);

  auto &tr = m_tr.at(child);
  tr.translation = trans;
  tr.rotation = rot;
  tr.scale = scale;
  tr.dirty = true;

  markWorldDirtyRecursive(child);

  m_events.push({WorldEventType::ParentChanged, child, newParent, oldParent});
  m_events.push({WorldEventType::TransformChanged, child});
}

EntityID World::cloneSubtree(EntityID root, EntityID newParent) {
  if (!isAlive(root))
    return InvalidEntity;

  updateTransforms();

  const glm::mat4 srcWorld = m_wtr.at(root).world;
  glm::mat4 parentWorld(1.0f);
  if (newParent != InvalidEntity && isAlive(newParent))
    parentWorld = m_wtr.at(newParent).world;

  const glm::mat4 newLocal = glm::inverse(parentWorld) * srcWorld;

  glm::vec3 skew;
  glm::vec4 persp;
  glm::vec3 scale;
  glm::quat rot;
  glm::vec3 trans;
  glm::decompose(newLocal, scale, rot, trans, skew, persp);

  EntityID dup = createEntity(m_name.at(root).name);

  auto &tr = m_tr.at(dup);
  tr.translation = trans;
  tr.rotation = rot;
  tr.scale = scale;
  tr.dirty = true;

  if (newParent != InvalidEntity && isAlive(newParent)) {
    attachToParent(dup, newParent);
    m_events.push({WorldEventType::ParentChanged, dup, newParent, InvalidEntity});
  }

  if (hasMesh(root))
    m_mesh[dup] = m_mesh.at(root);
  if (hasRenderableAsset(root))
    m_renderableAsset[dup] = m_renderableAsset.at(root);

  if (hasCamera(root)) {
    auto &cam = ensureCamera(dup);
    cam = m_cam.at(root);
    cam.dirty = true;

    auto &mats = m_camMat.at(dup);
    mats = m_camMat.at(root);
    mats.dirty = true;
  }

  markWorldDirtyRecursive(dup);

  EntityID ch = m_hier.at(root).firstChild;
  while (ch != InvalidEntity) {
    EntityID next = m_hier.at(ch).nextSibling;
    cloneSubtree(ch, dup);
    ch = next;
  }

  return dup;
}

static void duplicateMaterialsForSubtree(World &world,
                                         MaterialSystem &materials,
                                         EntityID root) {
  if (!world.isAlive(root))
    return;

  if (world.hasMesh(root)) {
    auto &mc = world.ensureMesh(root);
    for (auto &sm : mc.submeshes) {
      if (sm.material != InvalidMaterial && materials.isAlive(sm.material)) {
        MaterialData copy = materials.cpu(sm.material);
        sm.material = materials.create(copy);
      }
    }
  }

  EntityID c = world.hierarchy(root).firstChild;
  while (c != InvalidEntity) {
    EntityID next = world.hierarchy(c).nextSibling;
    duplicateMaterialsForSubtree(world, materials, c);
    c = next;
  }
}

EntityID World::duplicateSubtree(EntityID root, EntityID newParent,
                                 MaterialSystem *materials) {
  EntityID dup = cloneSubtree(root, newParent);
  if (dup == InvalidEntity)
    return InvalidEntity;
  if (materials)
    duplicateMaterialsForSubtree(*this, *materials, dup);
  return dup;
}

CName &World::name(EntityID e) { return m_name.at(e); }
const CName &World::name(EntityID e) const { return m_name.at(e); }

void World::setName(EntityID e, const std::string &n) {
  m_name.at(e).name = n;
  m_events.push({WorldEventType::NameChanged, e});
}

CTransform &World::transform(EntityID e) { return m_tr.at(e); }
const CTransform &World::transform(EntityID e) const { return m_tr.at(e); }

CWorldTransform &World::worldTransform(EntityID e) { return m_wtr.at(e); }
const CWorldTransform &World::worldTransform(EntityID e) const {
  return m_wtr.at(e);
}

glm::vec3 World::worldPosition(EntityID e) const {
  if (!isAlive(e))
    return glm::vec3(0.0f);
  const auto it = m_wtr.find(e);
  if (it == m_wtr.end())
    return glm::vec3(0.0f);
  return glm::vec3(it->second.world[3]);
}

glm::vec3 World::worldDirection(EntityID e, const glm::vec3 &localDir) const {
  if (!isAlive(e))
    return localDir;
  const auto it = m_wtr.find(e);
  if (it == m_wtr.end())
    return localDir;
  glm::vec4 worldDir = it->second.world * glm::vec4(localDir, 0.0f);
  return glm::normalize(glm::vec3(worldDir));
}

void World::updateTransforms() {
  auto updateNode = [&](auto &&self, EntityID e, const glm::mat4 &parentW,
                        bool parentDirty) -> void {
    auto &tr = m_tr.at(e);
    auto &wt = m_wtr.at(e);

    bool parentChanged = false;
    bool localChanged = false;
    if (parentDirty)
      wt.dirty = true;
    if (tr.dirty) {
      wt.dirty = true;
      tr.dirty = false;
      localChanged = true;
    }

    if (wt.dirty) {
      wt.world = parentW * localMatrix(e);
      wt.dirty = false;
      parentChanged = true;
    }

    if (localChanged || parentDirty)
      m_events.push({WorldEventType::TransformChanged, e});

    EntityID ch = m_hier.at(e).firstChild;
    while (ch != InvalidEntity) {
      EntityID next = m_hier.at(ch).nextSibling;
      self(self, ch, wt.world, parentChanged);
      ch = next;
    }
  };

  for (EntityID r : roots()) {
    updateNode(updateNode, r, glm::mat4(1.0f), false);
  }
}

} // namespace Nyx
