#include "World.h"

#include <algorithm>

namespace Nyx {

EntityID World::createEntity(const std::string &n) {
  const EntityID e = {m_next.index++, 1};
  m_alive.push_back(e);

  m_hier[e] = CHierarchy{};
  m_name[e] = CName{n};
  m_tr[e] = CTransform{};
  m_wtr[e] = CWorldTransform{};

  EntityUUID id = m_uuidGen.next();
  while (m_entityByUUID.find(id.value) != m_entityByUUID.end()) {
    id = m_uuidGen.next();
  }
  m_uuid[e] = id;
  m_entityByUUID[id.value] = e;

  m_events.push({WorldEventType::EntityCreated, e});
  return e;
}

EntityID World::createEntityWithUUID(EntityUUID uuid, const std::string &name) {
  if (!uuid)
    return createEntity(name);
  if (m_entityByUUID.find(uuid.value) != m_entityByUUID.end())
    return InvalidEntity;

  EntityID e = createEntity(name);

  const EntityUUID old = m_uuid[e];
  m_entityByUUID.erase(old.value);

  m_uuid[e] = uuid;
  m_entityByUUID[uuid.value] = e;
  return e;
}

bool World::isAlive(EntityID e) const {
  return e != InvalidEntity && m_hier.find(e) != m_hier.end();
}

void World::destroySubtree(EntityID root) {
  if (!isAlive(root))
    return;

  EntityID ch = m_hier[root].firstChild;
  while (ch != InvalidEntity) {
    EntityID next = m_hier[ch].nextSibling;
    destroySubtree(ch);
    ch = next;
  }

  if (hasCamera(root)) {
    m_events.push({WorldEventType::CameraDestroyed, root});
    if (m_activeCamera == root) {
      EntityID old = m_activeCamera;
      m_activeCamera = InvalidEntity;
      m_events.push({WorldEventType::ActiveCameraChanged, InvalidEntity, old});
    }
  }

  detachFromParent(root);
  clearEntityCategories(root);

  m_mesh.erase(root);
  m_renderableAsset.erase(root);
  m_cam.erase(root);
  m_camMat.erase(root);
  m_light.erase(root);

  m_hier.erase(root);
  m_name.erase(root);
  m_tr.erase(root);
  m_wtr.erase(root);
  auto uuidIt = m_uuid.find(root);
  if (uuidIt != m_uuid.end()) {
    m_entityByUUID.erase(uuidIt->second.value);
    m_uuid.erase(uuidIt);
  }

  auto it = std::find(m_alive.begin(), m_alive.end(), root);
  if (it != m_alive.end())
    m_alive.erase(it);

  m_events.push({WorldEventType::EntityDestroyed, root});
}

void World::destroyEntity(EntityID e) { destroySubtree(e); }

void World::clear() {
  m_next = {1, 0};
  m_alive.clear();

  m_hier.clear();
  m_name.clear();
  m_tr.clear();
  m_wtr.clear();

  m_mesh.clear();
  m_renderableAsset.clear();
  m_cam.clear();
  m_camMat.clear();
  m_light.clear();
  m_sky.clear();
  m_skySettings = CSky{};

  m_uuid.clear();
  m_entityByUUID.clear();
  m_categories.clear();
  m_entityCategories.clear();

  m_activeCamera = InvalidEntity;
  m_events.clear();
}

std::vector<EntityID> World::roots() const {
  std::vector<EntityID> r;
  r.reserve(m_alive.size());
  for (EntityID e : m_alive) {
    auto it = m_hier.find(e);
    if (it != m_hier.end() && it->second.parent == InvalidEntity)
      r.push_back(e);
  }
  return r;
}

CHierarchy &World::hierarchy(EntityID e) { return m_hier.at(e); }
const CHierarchy &World::hierarchy(EntityID e) const { return m_hier.at(e); }

EntityID World::parentOf(EntityID e) const {
  auto it = m_hier.find(e);
  if (it == m_hier.end())
    return InvalidEntity;
  return it->second.parent;
}

static bool isDescendant(const World &w, EntityID node,
                         EntityID potentialAncestor) {
  EntityID p = w.parentOf(node);
  while (p != InvalidEntity) {
    if (p == potentialAncestor)
      return true;
    p = w.parentOf(p);
  }
  return false;
}

void World::setParent(EntityID child, EntityID newParent) {
  if (!isAlive(child))
    return;

  if (newParent != InvalidEntity) {
    if (!isAlive(newParent))
      return;
    if (child == newParent)
      return;
    if (isDescendant(*this, newParent, child))
      return;
  }

  const EntityID oldParent = parentOf(child);
  if (oldParent == newParent)
    return;

  detachFromParent(child);
  attachToParent(child, newParent);

  markWorldDirtyRecursive(child);

  m_events.push({WorldEventType::ParentChanged, child, newParent, oldParent});
  m_events.push({WorldEventType::TransformChanged, child});
}

void World::detachFromParent(EntityID child) {
  auto &hc = m_hier.at(child);
  EntityID p = hc.parent;
  if (p == InvalidEntity)
    return;

  EntityID cur = m_hier.at(p).firstChild;
  EntityID prev = InvalidEntity;

  while (cur != InvalidEntity) {
    if (cur == child) {
      EntityID next = m_hier.at(cur).nextSibling;
      if (prev == InvalidEntity)
        m_hier.at(p).firstChild = next;
      else
        m_hier.at(prev).nextSibling = next;
      break;
    }
    prev = cur;
    cur = m_hier.at(cur).nextSibling;
  }

  hc.parent = InvalidEntity;
  hc.nextSibling = InvalidEntity;
}

void World::attachToParent(EntityID child, EntityID newParent) {
  auto &hc = m_hier.at(child);

  hc.parent = newParent;
  hc.nextSibling = InvalidEntity;

  if (newParent == InvalidEntity)
    return;

  auto &hp = m_hier.at(newParent);
  if (hp.firstChild == InvalidEntity) {
    hp.firstChild = child;
    return;
  }

  EntityID cur = hp.firstChild;
  while (m_hier.at(cur).nextSibling != InvalidEntity)
    cur = m_hier.at(cur).nextSibling;
  m_hier.at(cur).nextSibling = child;
}

} // namespace Nyx
