#include "World.h"

#include "render/material/MaterialSystem.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Nyx {

static bool vecContains(const std::vector<EntityID> &v, EntityID e) {
  return std::find(v.begin(), v.end(), e) != v.end();
}

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
  if (!uuid) {
    return createEntity(name);
  }
  if (m_entityByUUID.find(uuid.value) != m_entityByUUID.end()) {
    return InvalidEntity;
  }

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

  // Destroy children first
  EntityID ch = m_hier[root].firstChild;
  while (ch != InvalidEntity) {
    EntityID next = m_hier[ch].nextSibling;
    destroySubtree(ch);
    ch = next;
  }

  // Camera events
  if (hasCamera(root)) {
    m_events.push({WorldEventType::CameraDestroyed, root});
    if (m_activeCamera == root) {
      EntityID old = m_activeCamera;
      m_activeCamera = InvalidEntity;
      m_events.push({WorldEventType::ActiveCameraChanged, InvalidEntity, old});
    }
  }

  // Detach from parent
  detachFromParent(root);

  // Remove from categories
  clearEntityCategories(root);

  // Erase optional comps
  m_mesh.erase(root);
  m_cam.erase(root);
  m_camMat.erase(root);
  m_light.erase(root);

  // Erase core comps
  m_hier.erase(root);
  m_name.erase(root);
  m_tr.erase(root);
  m_wtr.erase(root);
  auto uuidIt = m_uuid.find(root);
  if (uuidIt != m_uuid.end()) {
    m_entityByUUID.erase(uuidIt->second.value);
    m_uuid.erase(uuidIt);
  }

  // Remove from alive vector (linear ok for now)
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

// ---------------- Hierarchy ----------------

CHierarchy &World::hierarchy(EntityID e) { return m_hier.at(e); }
const CHierarchy &World::hierarchy(EntityID e) const { return m_hier.at(e); }

EntityID World::parentOf(EntityID e) const {
  auto it = m_hier.find(e);
  if (it == m_hier.end())
    return InvalidEntity;
  return it->second.parent;
}

static bool isDescendant(const World &w, EntityID node,
                         EntityID potentialAncestor);

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

  // Remove child from parent's singly-linked child list
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

  // append at end (deterministic)
  EntityID cur = hp.firstChild;
  while (m_hier.at(cur).nextSibling != InvalidEntity)
    cur = m_hier.at(cur).nextSibling;
  m_hier.at(cur).nextSibling = child;
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

  // Don’t allow cycles
  if (newParent != InvalidEntity) {
    if (!isAlive(newParent))
      return;
    if (child == newParent)
      return;
    if (isDescendant(*this, newParent, child))
      return;
  }

  // Ensure transforms are up to date before we preserve world
  updateTransforms();

  const EntityID oldParent = parentOf(child);
  const glm::mat4 oldWorld = m_wtr.at(child).world;

  // Reparent
  detachFromParent(child);
  attachToParent(child, newParent);

  // Recompute local so that world remains the same
  glm::mat4 parentWorld(1.0f);
  if (newParent != InvalidEntity) {
    parentWorld = m_wtr.at(newParent).world;
  }

  const glm::mat4 newLocal = glm::inverse(parentWorld) * oldWorld;

  // Decompose newLocal into T/R/S (simple approach)
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

  // Copy optional components.
  if (hasMesh(root)) {
    m_mesh[dup] = m_mesh.at(root);
  }

  if (hasCamera(root)) {
    auto &cam = ensureCamera(dup);
    cam = m_cam.at(root);
    cam.dirty = true;

    auto &mats = m_camMat.at(dup);
    mats = m_camMat.at(root);
    mats.dirty = true;
  }

  markWorldDirtyRecursive(dup);

  // Clone children (preserve hierarchy order).
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

// ---------------- Name ----------------

CName &World::name(EntityID e) { return m_name.at(e); }
const CName &World::name(EntityID e) const { return m_name.at(e); }

void World::setName(EntityID e, const std::string &n) {
  m_name.at(e).name = n;
  m_events.push({WorldEventType::NameChanged, e});
}

// ---------------- Transform ----------------

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
  // Transform direction by world matrix (ignore translation)
  glm::vec4 worldDir = it->second.world * glm::vec4(localDir, 0.0f);
  return glm::normalize(glm::vec3(worldDir));
}

void World::updateTransforms() {
  // Roots first; DFS propagate
  // For now we recompute if dirty flags indicate changes; still fast enough.

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

    if (localChanged || parentDirty) {
      m_events.push({WorldEventType::TransformChanged, e});
    }

    // Children inherit
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

// ---------------- Mesh ----------------

bool World::hasMesh(EntityID e) const { return m_mesh.find(e) != m_mesh.end(); }

CMesh &World::ensureMesh(EntityID e) {
  auto it = m_mesh.find(e);
  if (it != m_mesh.end())
    return it->second;

  CMesh mc{};
  mc.submeshes.push_back(MeshSubmesh{});
  m_mesh.emplace(e, mc);

  m_events.push({WorldEventType::MeshChanged, e});
  return m_mesh.at(e);
}

CMesh &World::mesh(EntityID e) { return m_mesh.at(e); }
const CMesh &World::mesh(EntityID e) const { return m_mesh.at(e); }

void World::removeMesh(EntityID e) {
  auto it = m_mesh.find(e);
  if (it == m_mesh.end())
    return;
  m_mesh.erase(it);
  m_events.push({WorldEventType::MeshChanged, e});
}

uint32_t World::submeshCount(EntityID e) const {
  auto it = m_mesh.find(e);
  if (it == m_mesh.end())
    return 0;
  return (uint32_t)it->second.submeshes.size();
}

MeshSubmesh &World::submesh(EntityID e, uint32_t si) {
  auto &mc = ensureMesh(e);
  if (mc.submeshes.empty())
    mc.submeshes.push_back(MeshSubmesh{});
  if (si >= (uint32_t)mc.submeshes.size())
    mc.submeshes.resize((size_t)si + 1, MeshSubmesh{});
  return mc.submeshes[(size_t)si];
}

// ---------------- Camera ----------------

bool World::hasCamera(EntityID e) const { return m_cam.find(e) != m_cam.end(); }

CCamera &World::ensureCamera(EntityID e) {
  auto it = m_cam.find(e);
  if (it != m_cam.end())
    return it->second;

  m_cam[e] = CCamera{};
  m_camMat[e] = CCameraMatrices{};
  m_events.push({WorldEventType::CameraCreated, e});

  // If no active camera yet, make it active automatically
  if (m_activeCamera == InvalidEntity) {
    setActiveCamera(e);
  }
  return m_cam.at(e);
}

CCamera &World::camera(EntityID e) { return m_cam.at(e); }
const CCamera &World::camera(EntityID e) const { return m_cam.at(e); }

CCameraMatrices &World::cameraMatrices(EntityID e) { return m_camMat.at(e); }
const CCameraMatrices &World::cameraMatrices(EntityID e) const {
  return m_camMat.at(e);
}

void World::removeCamera(EntityID e) {
  auto it = m_cam.find(e);
  if (it == m_cam.end())
    return;
  m_cam.erase(it);
  m_camMat.erase(e);
  m_events.push({WorldEventType::CameraDestroyed, e});
  if (m_activeCamera == e) {
    EntityID old = m_activeCamera;
    m_activeCamera = InvalidEntity;
    m_events.push({WorldEventType::ActiveCameraChanged, InvalidEntity, old});
  }
}

// ---------------- Light ----------------

bool World::hasLight(EntityID e) const {
  return m_light.find(e) != m_light.end();
}

CLight &World::ensureLight(EntityID e) {
  auto it = m_light.find(e);
  if (it != m_light.end())
    return it->second;

  m_light[e] = CLight{};
  if (!hasMesh(e)) {
    auto &mc = ensureMesh(e);
    if (mc.submeshes.empty())
      mc.submeshes.push_back(MeshSubmesh{});
    mc.submeshes[0].name = "Light";
    mc.submeshes[0].type = ProcMeshType::Sphere;
  }
  return m_light.at(e);
}

CLight &World::light(EntityID e) { return m_light.at(e); }
const CLight &World::light(EntityID e) const { return m_light.at(e); }

void World::removeLight(EntityID e) {
  auto it = m_light.find(e);
  if (it == m_light.end())
    return;
  m_light.erase(it);
  m_events.push({WorldEventType::LightChanged, e});
}

// ---- Sky ----

bool World::hasSky(EntityID e) const {
  return m_sky.find(e) != m_sky.end();
}

CSky &World::ensureSky(EntityID e) {
  auto it = m_sky.find(e);
  if (it != m_sky.end())
    return it->second;

  m_sky[e] = CSky{};
  return m_sky.at(e);
}

CSky &World::sky(EntityID e) { return m_sky.at(e); }
const CSky &World::sky(EntityID e) const { return m_sky.at(e); }

CSky &World::skySettings() { return m_skySettings; }
const CSky &World::skySettings() const { return m_skySettings; }

void World::setActiveCamera(EntityID cam) {
  if (cam != InvalidEntity && !isAlive(cam))
    return;
  if (cam != InvalidEntity && !hasCamera(cam))
    return;
  if (cam != InvalidEntity) {
    const auto &tr = transform(cam);
    if (tr.hidden || tr.hiddenEditor || tr.disabledAnim)
      return;
  }

  if (m_activeCamera == cam)
    return;

  const EntityID old = m_activeCamera;
  m_activeCamera = cam;

  if (m_activeCamera != InvalidEntity) {
    auto it = m_cam.find(m_activeCamera);
    if (it != m_cam.end())
      it->second.dirty = true;
    auto mit = m_camMat.find(m_activeCamera);
    if (mit != m_camMat.end())
      mit->second.dirty = true;
  }

  m_events.push({WorldEventType::ActiveCameraChanged, cam, old});
}

uint32_t World::addCategory(const std::string &name) {
  Category c{};
  c.name = name;
  m_categories.push_back(std::move(c));
  return (uint32_t)(m_categories.size() - 1u);
}

void World::removeCategory(uint32_t idx) {
  if (idx >= m_categories.size())
    return;
  const int32_t parent = m_categories[idx].parent;
  for (EntityID e : m_categories[idx].entities) {
    auto it = m_entityCategories.find(e);
    if (it != m_entityCategories.end()) {
      auto &lst = it->second;
      lst.erase(std::remove(lst.begin(), lst.end(), idx), lst.end());
      if (lst.empty())
        m_entityCategories.erase(it);
    }
  }
  // Reparent children to our parent
  for (uint32_t child : m_categories[idx].children) {
    if (child < m_categories.size())
      m_categories[child].parent = parent;
  }
  if (parent >= 0 && parent < (int32_t)m_categories.size()) {
    auto &vec = m_categories[(size_t)parent].children;
    vec.erase(std::remove(vec.begin(), vec.end(), idx), vec.end());
    for (uint32_t child : m_categories[idx].children) {
      if (std::find(vec.begin(), vec.end(), child) == vec.end())
        vec.push_back(child);
    }
  }

  m_categories.erase(m_categories.begin() + (ptrdiff_t)idx);
  // Fix indices in map
  for (auto &kv : m_entityCategories) {
    for (uint32_t &v : kv.second) {
      if (v > idx)
        v--;
    }
  }
  for (auto &c : m_categories) {
    if (c.parent >= (int32_t)idx)
      c.parent--;
    for (uint32_t &ch : c.children) {
      if (ch > idx)
        ch--;
    }
  }
}

void World::renameCategory(uint32_t idx, const std::string &name) {
  if (idx >= m_categories.size())
    return;
  m_categories[idx].name = name;
}

void World::addEntityCategory(EntityID e, int32_t idx) {
  if (e == InvalidEntity)
    return;
  if (idx < 0 || idx >= (int32_t)m_categories.size())
    return;

  auto &dst = m_categories[(size_t)idx].entities;
  if (std::find(dst.begin(), dst.end(), e) == dst.end())
    dst.push_back(e);

  auto &lst = m_entityCategories[e];
  if (std::find(lst.begin(), lst.end(), (uint32_t)idx) == lst.end())
    lst.push_back((uint32_t)idx);
}

void World::removeEntityCategory(EntityID e, int32_t idx) {
  if (e == InvalidEntity)
    return;
  if (idx < 0 || idx >= (int32_t)m_categories.size())
    return;
  auto &vec = m_categories[(size_t)idx].entities;
  vec.erase(std::remove(vec.begin(), vec.end(), e), vec.end());

  auto it = m_entityCategories.find(e);
  if (it != m_entityCategories.end()) {
    auto &lst = it->second;
    lst.erase(std::remove(lst.begin(), lst.end(), (uint32_t)idx), lst.end());
    if (lst.empty())
      m_entityCategories.erase(it);
  }
}

void World::clearEntityCategories(EntityID e) {
  if (e == InvalidEntity)
    return;
  auto it = m_entityCategories.find(e);
  if (it == m_entityCategories.end())
    return;
  for (uint32_t idx : it->second) {
    if (idx < m_categories.size()) {
      auto &vec = m_categories[idx].entities;
      vec.erase(std::remove(vec.begin(), vec.end(), e), vec.end());
    }
  }
  m_entityCategories.erase(it);
}

const std::vector<uint32_t> *World::entityCategories(EntityID e) const {
  auto it = m_entityCategories.find(e);
  if (it == m_entityCategories.end())
    return nullptr;
  return &it->second;
}

void World::setCategoryParent(uint32_t idx, int32_t parentIdx) {
  if (idx >= m_categories.size())
    return;
  if (parentIdx >= (int32_t)m_categories.size())
    return;
  if ((int32_t)idx == parentIdx)
    return;

  const int32_t old = m_categories[idx].parent;
  if (old >= 0 && old < (int32_t)m_categories.size()) {
    auto &vec = m_categories[(size_t)old].children;
    vec.erase(std::remove(vec.begin(), vec.end(), idx), vec.end());
  }
  m_categories[idx].parent = parentIdx;
  if (parentIdx >= 0) {
    auto &vec = m_categories[(size_t)parentIdx].children;
    if (std::find(vec.begin(), vec.end(), idx) == vec.end())
      vec.push_back(idx);
  }
}

void World::setActiveCameraUUID(EntityUUID id) {
  if (!id) {
    setActiveCamera(InvalidEntity);
    return;
  }
  EntityID e = findByUUID(id);
  if (e == InvalidEntity)
    return;
  setActiveCamera(e);
}

EntityUUID World::uuid(EntityID e) const {
  auto it = m_uuid.find(e);
  if (it == m_uuid.end())
    return EntityUUID{0};
  return it->second;
}

EntityID World::findByUUID(EntityUUID uuid) const {
  if (!uuid)
    return InvalidEntity;
  auto it = m_entityByUUID.find(uuid.value);
  if (it == m_entityByUUID.end())
    return InvalidEntity;
  return it->second;
}

void World::setUUIDSeed(uint64_t seed) { m_uuidGen.setSeed(seed); }

} // namespace Nyx
