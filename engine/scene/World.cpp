#include "World.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "../core/Assert.h"

namespace Nyx {

// ----------------------------
// EntityID packing contract
// ----------------------------
// We pack EntityID as: [generation:12][index:20] (matches your pick layout nicely)
// index range: 1..1,048,575
// generation: 0..4095
//
// InvalidEntity is assumed to be 0.
//
// If your existing EntityID is different, adapt idx/gen/makeID accordingly.

static constexpr std::uint32_t kIndexBits = 20u;
static constexpr std::uint32_t kIndexMask = (1u << kIndexBits) - 1u;
static constexpr std::uint32_t kGenBits = 12u;
static constexpr std::uint32_t kGenMask = (1u << kGenBits) - 1u;

std::uint32_t World::idx(EntityID e) {
  return static_cast<std::uint32_t>(e) & kIndexMask;
}

std::uint32_t World::gen(EntityID e) const {
  return (static_cast<std::uint32_t>(e) >> kIndexBits) & kGenMask;
}

EntityID World::makeID(std::uint32_t index, std::uint32_t generation) const {
  const std::uint32_t packed =
      (index & kIndexMask) | ((generation & kGenMask) << kIndexBits);
  return static_cast<EntityID>(packed);
}

// ----------------------------

World::World() {
  // Reserve slot 0 as "invalid"
  m_slots.resize(1);
  m_name.resize(1);
  m_transform.resize(1);
  m_world.resize(1);
  m_hierarchy.resize(1);

  m_hasWorld.resize(1, false);
  m_hasHierarchy.resize(1, false);

  m_mesh.resize(1);
  m_hasMesh.resize(1, false);
}

World::~World() = default;

void World::ensureCapacity(std::uint32_t index) {
  if (index < m_slots.size()) return;

  const std::uint32_t newSize = index + 1u;

  m_slots.resize(newSize);
  m_name.resize(newSize);
  m_transform.resize(newSize);
  m_world.resize(newSize);
  m_hierarchy.resize(newSize);

  m_hasWorld.resize(newSize, false);
  m_hasHierarchy.resize(newSize, false);

  m_mesh.resize(newSize);
  m_hasMesh.resize(newSize, false);

}

EntityID World::createEntity(std::string nm) {
  std::uint32_t index = 0;

  if (m_freeHead != 0) {
    index = m_freeHead;
    NYX_ASSERT(index < m_slots.size(), "free list corrupted");
    m_freeHead = m_slots[index].nextFree;
  } else {
    index = static_cast<std::uint32_t>(m_slots.size());
    ensureCapacity(index);
  }

  auto& s = m_slots[index];
  s.alive = true;
  // generation stays as-is, only increments on destroy

  m_name[index].name = std::move(nm);
  m_transform[index] = CTransform{};
  m_hasWorld[index] = true;
  m_world[index] = CWorldTransform{};
  m_hasHierarchy[index] = true;
  m_hierarchy[index] = CHierarchy{};

  m_hasMesh[index] = false;
  m_mesh[index] = CMesh{};

  const EntityID id = makeID(index, s.generation);
  m_aliveList.push_back(id);
  return id;
}

bool World::isAlive(EntityID e) const {
  const std::uint32_t index = idx(e);
  if (index == 0 || index >= m_slots.size()) return false;
  const auto& s = m_slots[index];
  return s.alive && s.generation == gen(e);
}

EntityID World::entityFromSlotIndex(EntityID slotIndex) const {
  const std::uint32_t index = idx(slotIndex);
  if (index == 0 || index >= m_slots.size())
    return InvalidEntity;
  const auto& s = m_slots[index];
  if (!s.alive)
    return InvalidEntity;
  return makeID(index, s.generation);
}

void World::removeFromAliveList(EntityID e) {
  auto it = std::find(m_aliveList.begin(), m_aliveList.end(), e);
  if (it != m_aliveList.end()) {
    m_aliveList.erase(it);
  }
}

void World::destroyEntity(EntityID e) {
  if (!isAlive(e)) return;

  // Destroy children first (safe, consistent)
  EntityID child = hierarchy(e).firstChild;
  while (child != InvalidEntity) {
    EntityID next = hierarchy(child).nextSibling;
    destroyEntity(child);
    child = next;
  }

  // Detach from parent list
  detachFromParentList(e);

  const std::uint32_t index = idx(e);
  auto& s = m_slots[index];
  s.alive = false;
  s.generation = (s.generation + 1u) & kGenMask;
  if (s.generation == 0u) s.generation = 1u;

  // Clear hierarchy links
  m_hierarchy[index] = CHierarchy{};
  m_hasHierarchy[index] = true;

  // Reset mesh state so reused slots don't keep old data.
  m_mesh[index] = CMesh{};
  m_hasMesh[index] = false;

  // Return to free list
  s.nextFree = m_freeHead;
  m_freeHead = index;

  removeFromAliveList(e);
}

// ----------------------------
// Component accessors
// ----------------------------

CName& World::name(EntityID e) {
  NYX_ASSERT(isAlive(e), "name(): entity not alive");
  return m_name[idx(e)];
}

const CName& World::name(EntityID e) const {
  NYX_ASSERT(isAlive(e), "name() const: entity not alive");
  return m_name[idx(e)];
}

void World::setName(EntityID e, std::string n) {
  name(e).name = std::move(n);
}

CTransform& World::transform(EntityID e) {
  NYX_ASSERT(isAlive(e), "transform(): entity not alive");
  return m_transform[idx(e)];
}

const CTransform& World::transform(EntityID e) const {
  NYX_ASSERT(isAlive(e), "transform() const: entity not alive");
  return m_transform[idx(e)];
}

bool World::hasWorldTransform(EntityID e) const {
  if (!isAlive(e)) return false;
  return m_hasWorld[idx(e)];
}

CWorldTransform& World::worldTransform(EntityID e) {
  NYX_ASSERT(isAlive(e), "worldTransform(): entity not alive");
  NYX_ASSERT(m_hasWorld[idx(e)], "worldTransform(): missing component");
  return m_world[idx(e)];
}

const CWorldTransform& World::worldTransform(EntityID e) const {
  NYX_ASSERT(isAlive(e), "worldTransform() const: entity not alive");
  NYX_ASSERT(m_hasWorld[idx(e)], "worldTransform() const: missing component");
  return m_world[idx(e)];
}

bool World::hasHierarchy(EntityID e) const {
  if (!isAlive(e)) return false;
  return m_hasHierarchy[idx(e)];
}

CHierarchy& World::hierarchy(EntityID e) {
  NYX_ASSERT(isAlive(e), "hierarchy(): entity not alive");
  NYX_ASSERT(m_hasHierarchy[idx(e)], "hierarchy(): missing component");
  return m_hierarchy[idx(e)];
}

const CHierarchy& World::hierarchy(EntityID e) const {
  NYX_ASSERT(isAlive(e), "hierarchy() const: entity not alive");
  NYX_ASSERT(m_hasHierarchy[idx(e)], "hierarchy() const: missing component");
  return m_hierarchy[idx(e)];
}

// ----------------------------
// Mesh / Materials
// ----------------------------

bool World::hasMesh(EntityID e) const {
  if (!isAlive(e)) return false;
  const std::uint32_t i = idx(e);
  return m_hasMesh[i] || !m_mesh[i].submeshes.empty();
}

CMesh& World::mesh(EntityID e) {
  NYX_ASSERT(isAlive(e), "mesh(): entity not alive");
  const std::uint32_t i = idx(e);
  if (!m_hasMesh[i] && !m_mesh[i].submeshes.empty())
    m_hasMesh[i] = true;
  NYX_ASSERT(m_hasMesh[i], "mesh(): missing component");
  return m_mesh[idx(e)];
}

const CMesh& World::mesh(EntityID e) const {
  NYX_ASSERT(isAlive(e), "mesh() const: entity not alive");
  const std::uint32_t i = idx(e);
  NYX_ASSERT(m_hasMesh[i] || !m_mesh[i].submeshes.empty(),
             "mesh() const: missing component");
  return m_mesh[idx(e)];
}

CMesh& World::ensureMesh(EntityID e, ProcMeshType t, std::uint32_t submeshCount_) {
  NYX_ASSERT(isAlive(e), "ensureMesh(): entity not alive");

  const std::uint32_t i = idx(e);
  m_hasMesh[i] = true;
  m_mesh[i].submeshes.resize(std::max(1u, submeshCount_));
  m_mesh[i].submeshes[submeshCount_ - 1].type = t;

  // Ensure material slots exist

  return m_mesh[i];
}

std::uint32_t World::submeshCount(EntityID e) const {
  if (!isAlive(e)) return 0;
  const std::uint32_t i = idx(e);
  if (!m_hasMesh[i] && m_mesh[i].submeshes.empty())
    return 0;
  return (uint32_t)m_mesh[i].submeshes.size();
}

MaterialHandle World::materialHandle(EntityID e, std::uint32_t submeshIndex) {
  NYX_ASSERT(isAlive(e), "materialHandle(): entity not alive");

  const std::uint32_t i = idx(e);

  // If no mesh, still allow "material storage" later — but for now,
  // we assume materials are tied to mesh existence.
  if (!m_hasMesh[i]) {
    // Create a default procedural mesh contract: 1 submesh
    ensureMesh(e, ProcMeshType::Cube, 1);
  }

  const std::uint32_t n = m_mesh[i].submeshes.size();
  NYX_ASSERT(submeshIndex < n, "materialHandle(): submesh index out of range");

  auto &mesh = m_mesh[i];
  return mesh.submeshes[submeshIndex].material;
}

void World::setMaterialHandle(EntityID e, std::uint32_t submeshIndex, MaterialHandle h) {
  NYX_ASSERT(isAlive(e), "setMaterialHandle(): entity not alive");

  const std::uint32_t i = idx(e);
  if (!m_hasMesh[i]) {
    ensureMesh(e, ProcMeshType::Cube, 1);
  }

  const std::uint32_t n = m_mesh[i].submeshes.size();
  NYX_ASSERT(submeshIndex < n, "setMaterialHandle(): submesh index out of range");

  auto &mesh = m_mesh[i];
  mesh.submeshes[submeshIndex].material = h;
}

// ----------------------------
// Hierarchy internals
// ----------------------------

EntityID World::parentOf(EntityID e) const {
  if (!isAlive(e)) return InvalidEntity;
  return hierarchy(e).parent;
}

void World::detachFromParentList(EntityID child) {
  if (!isAlive(child)) return;

  auto& hc = hierarchy(child);
  const EntityID parent = hc.parent;

  if (parent == InvalidEntity) {
    hc.parent = InvalidEntity;
    return;
  }

  // Remove from parent's child list
  auto& hp = hierarchy(parent);

  EntityID prev = InvalidEntity;
  EntityID cur = hp.firstChild;
  while (cur != InvalidEntity) {
    EntityID next = hierarchy(cur).nextSibling;
    if (cur == child) {
      if (prev == InvalidEntity) hp.firstChild = next;
      else hierarchy(prev).nextSibling = next;

      hc.parent = InvalidEntity;
      hc.nextSibling = InvalidEntity;
      return;
    }
    prev = cur;
    cur = next;
  }

  // If we didn't find it, still clear pointers safely
  hc.parent = InvalidEntity;
  hc.nextSibling = InvalidEntity;
}

void World::attachToParentFront(EntityID child, EntityID parent) {
  NYX_ASSERT(isAlive(child), "attachToParentFront(): child not alive");

  auto& hc = hierarchy(child);

  if (parent == InvalidEntity) {
    hc.parent = InvalidEntity;
    hc.nextSibling = InvalidEntity;
    return;
  }

  NYX_ASSERT(isAlive(parent), "attachToParentFront(): parent not alive");
  auto& hp = hierarchy(parent);

  hc.parent = parent;
  hc.nextSibling = hp.firstChild;
  hp.firstChild = child;
}

// Prevent cycles (basic)
static bool isAncestor(const World& w, EntityID maybeAncestor, EntityID node) {
  EntityID p = w.parentOf(node);
  while (p != InvalidEntity) {
    if (p == maybeAncestor) return true;
    p = w.parentOf(p);
  }
  return false;
}

void World::setParent(EntityID child, EntityID newParent) {
  if (!isAlive(child)) return;
  if (newParent == child) return;
  if (newParent != InvalidEntity && !isAlive(newParent)) return;
  if (newParent != InvalidEntity && isAncestor(*this, child, newParent)) return;

  detachFromParentList(child);
  attachToParentFront(child, newParent);

  // Mark dirty for world recompute
  if (hasWorldTransform(child)) worldTransform(child).dirty = true;
  transform(child).dirty = true;
}

glm::mat4 World::computeWorldMatrix(EntityID e) const {
  if (!isAlive(e)) return glm::mat4(1.0f);
  if (hasWorldTransform(e)) return worldTransform(e).world;
  return glm::mat4(1.0f);
}

void World::setLocalFromMatrix(EntityID e, const glm::mat4& local) {
  auto& t = transform(e);

  // Translation
  t.translation = glm::vec3(local[3]);

  // Extract scale from basis
  glm::vec3 bx = glm::vec3(local[0]);
  glm::vec3 by = glm::vec3(local[1]);
  glm::vec3 bz = glm::vec3(local[2]);

  t.scale = glm::vec3(glm::length(bx), glm::length(by), glm::length(bz));
  const float sx = (t.scale.x == 0.0f) ? 1.0f : t.scale.x;
  const float sy = (t.scale.y == 0.0f) ? 1.0f : t.scale.y;
  const float sz = (t.scale.z == 0.0f) ? 1.0f : t.scale.z;

  glm::mat3 R;
  R[0] = bx / sx;
  R[1] = by / sy;
  R[2] = bz / sz;

  t.rotation = glm::quat_cast(R);

  t.dirty = true;
  if (hasWorldTransform(e)) worldTransform(e).dirty = true;
}

void World::setParentKeepWorld(EntityID child, EntityID newParent) {
  if (!isAlive(child)) return;
  if (newParent == child) return;
  if (newParent != InvalidEntity && !isAlive(newParent)) return;
  if (newParent != InvalidEntity && isAncestor(*this, child, newParent)) return;

  // Compute old world (cached if available)
  const glm::mat4 oldWorld = computeWorldMatrix(child);

  // Detach and attach in hierarchy
  detachFromParentList(child);
  attachToParentFront(child, newParent);

  // New parent world
  glm::mat4 parentWorld(1.0f);
  if (newParent != InvalidEntity) {
    parentWorld = computeWorldMatrix(newParent);
  }

  // local = inv(parentWorld) * oldWorld
  const glm::mat4 newLocal = glm::inverse(parentWorld) * oldWorld;
  setLocalFromMatrix(child, newLocal);
}

// ----------------------------
// Roots
// ----------------------------

std::vector<EntityID> World::roots() const {
  std::vector<EntityID> out;
  out.reserve(m_aliveList.size());

  for (EntityID e : m_aliveList) {
    if (!isAlive(e)) continue;
    if (hierarchy(e).parent == InvalidEntity) out.push_back(e);
  }
  return out;
}

// ----------------------------
// Cloning
// ----------------------------

EntityID World::cloneEntityShallow(EntityID src) {
  NYX_ASSERT(isAlive(src), "cloneEntityShallow(): src dead");

  EntityID e = createEntity(name(src).name + " Copy");

  // Copy transform
  transform(e) = transform(src);
  transform(e).dirty = true;

  // Preserve world so setParentKeepWorld can compute the right local
  if (hasWorldTransform(e) && hasWorldTransform(src)) {
    worldTransform(e).world = worldTransform(src).world;
    worldTransform(e).dirty = true;
  }

  // Copy mesh
  if (hasMesh(src)) {
    const auto& sm = mesh(src);
    auto& dstMesh =
        ensureMesh(e, sm.submeshes.front().type, sm.submeshes.size());

    const std::uint32_t n = sm.submeshes.size();
    for (std::uint32_t i = 0; i < n; ++i) {
      dstMesh.submeshes[i].material = sm.submeshes[i].material;
    }
  }

  return e;
}

EntityID World::cloneSubtree(EntityID src, EntityID newParent) {
  if (!isAlive(src)) return InvalidEntity;

  EntityID rootCopy = cloneEntityShallow(src);
  setParentKeepWorld(rootCopy, newParent);

  // Recursively clone children
  EntityID c = hierarchy(src).firstChild;
  while (c != InvalidEntity) {
    EntityID next = hierarchy(c).nextSibling;
    cloneSubtree(c, rootCopy);
    c = next;
  }

  return rootCopy;
}

} // namespace Nyx
