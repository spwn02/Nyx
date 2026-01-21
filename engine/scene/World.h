#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <span>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "EntityID.h"
#include "Components.h"
#include "TransformSystem.h"

namespace Nyx {

class World final {
public:
  World();
  ~World();

  World(const World&) = delete;
  World& operator=(const World&) = delete;

  // ----------------------------
  // Entity lifetime
  // ----------------------------
  EntityID createEntity(std::string name = "Entity");
  void destroyEntity(EntityID e);

  bool isAlive(EntityID e) const;

  // Reconstructs the current live EntityID for a slot index as stored in pick IDs.
  // Returns InvalidEntity if the slot is empty or the entity is dead.
  EntityID entityFromSlotIndex(EntityID slotIndex) const;

  // ----------------------------
  // Core components (always exist)
  // ----------------------------
  CName& name(EntityID e);
  const CName& name(EntityID e) const;

  void setName(EntityID e, std::string n);

  CTransform& transform(EntityID e);
  const CTransform& transform(EntityID e) const;

  bool hasWorldTransform(EntityID e) const;
  CWorldTransform& worldTransform(EntityID e);
  const CWorldTransform& worldTransform(EntityID e) const;

  bool hasHierarchy(EntityID e) const;
  CHierarchy& hierarchy(EntityID e);
  const CHierarchy& hierarchy(EntityID e) const;

  // ----------------------------
  // Mesh / Materials
  // ----------------------------
  bool hasMesh(EntityID e) const;
  CMesh& mesh(EntityID e);
  const CMesh& mesh(EntityID e) const;

  // Returns a reference to the per-entity mesh component, creating it if absent.
  CMesh& ensureMesh(EntityID e, ProcMeshType t, std::uint32_t submeshCount = 1);

  // Submesh materials (per entity)
  // Creates slots as needed; returns handle (0 means "unassigned", but slot exists)
  MaterialHandle materialHandle(EntityID e, std::uint32_t submeshIndex);
  void setMaterialHandle(EntityID e, std::uint32_t submeshIndex, MaterialHandle h);

  std::uint32_t submeshCount(EntityID e) const;

  // ----------------------------
  // Hierarchy ops
  // ----------------------------
  std::vector<EntityID> roots() const;
  EntityID parentOf(EntityID e) const;

  void setParent(EntityID child, EntityID newParent);
  void setParentKeepWorld(EntityID child, EntityID newParent);

  // Clone utilities (Phase-2A)
  EntityID cloneSubtree(EntityID src, EntityID newParent);

  // ----------------------------
  // Transform propagation support
  // ----------------------------
  void updateTransforms() { m_transformSystem.update(*this); }

  std::span<const EntityID> aliveEntities() const { return m_aliveList; }

private:
  TransformSystem m_transformSystem;

  struct Slot final {
    std::uint32_t generation = 1;
    bool alive = false;

    // For free list
    std::uint32_t nextFree = 0;
  };

  // Storage (sparse slots)
  std::vector<Slot> m_slots;
  std::uint32_t m_freeHead = 0; // 0 means none

  // Alive list for iteration
  std::vector<EntityID> m_aliveList;

  // Components (parallel arrays)
  std::vector<CName> m_name;
  std::vector<CTransform> m_transform;
  std::vector<CWorldTransform> m_world;
  std::vector<CHierarchy> m_hierarchy;

  std::vector<bool> m_hasWorld;
  std::vector<bool> m_hasHierarchy;

  std::vector<CMesh> m_mesh;
  std::vector<bool> m_hasMesh;
private:
  static std::uint32_t idx(EntityID e);
  EntityID makeID(std::uint32_t index, std::uint32_t generation) const;
  std::uint32_t gen(EntityID e) const;

  void ensureCapacity(std::uint32_t index);

  void detachFromParentList(EntityID child);
  void attachToParentFront(EntityID child, EntityID parent);

  glm::mat4 computeWorldMatrix(EntityID e) const;
  void setLocalFromMatrix(EntityID e, const glm::mat4& local);

  void removeFromAliveList(EntityID e);

  EntityID cloneEntityShallow(EntityID src);

};

} // namespace Nyx
