#pragma once

#include "Camera.h"
#include "Components.h"
#include "EntityID.h"
#include "EntityUUID.h"
#include "WorldEvents.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nyx {

// ------------------------------
// Dense+Sparse component storage
// ------------------------------
template <typename T> class ComponentPool final {
public:
  bool has(EntityID e) const {
    const auto it = m_sparse.find(e.index);
    if (it == m_sparse.end())
      return false;
    const uint32_t denseIdx = it->second;
    return denseIdx < m_denseEntities.size() && m_denseEntities[denseIdx] == e;
  }

  T &get(EntityID e) { return m_dense[denseIndex(e)]; }
  const T &get(EntityID e) const { return m_dense[denseIndex(e)]; }

  template <class... Args> T &ensure(EntityID e, Args &&...args) {
    if (has(e))
      return get(e);
    const uint32_t idx = (uint32_t)m_dense.size();
    m_denseEntities.push_back(e);
    m_dense.emplace_back(T{std::forward<Args>(args)...});
    m_sparse[e.index] = idx;
    return m_dense.back();
  }

  void remove(EntityID e) {
    const auto it = m_sparse.find(e.index);
    if (it == m_sparse.end())
      return;

    const uint32_t idx = it->second;
    if (idx >= m_denseEntities.size() || m_denseEntities[idx] != e) {
      m_sparse.erase(it);
      return;
    }

    const uint32_t last = (uint32_t)m_dense.size() - 1u;
    if (idx != last) {
      m_dense[idx] = std::move(m_dense[last]);
      m_denseEntities[idx] = m_denseEntities[last];
      m_sparse[m_denseEntities[idx].index] = idx;
    }

    m_dense.pop_back();
    m_denseEntities.pop_back();
    m_sparse.erase(it);
  }

  void clear() {
    m_dense.clear();
    m_denseEntities.clear();
    m_sparse.clear();
  }

private:
  uint32_t denseIndex(EntityID e) const {
    const auto it = m_sparse.find(e.index);
    return (it == m_sparse.end()) ? ~0u : it->second;
  }

  std::vector<T> m_dense;
  std::vector<EntityID> m_denseEntities;
  std::unordered_map<uint32_t, uint32_t>
      m_sparse; // entity.index -> dense index
};

// ------------------------------
// World
// ------------------------------
class World final {
public:
  World() = default;

  // ---- Events ----
  WorldEvents &events() { return m_events; }
  const WorldEvents &events() const { return m_events; }
  void clearEvents() { m_events.clear(); }

  // ---- Entity lifecycle ----
  EntityID createEntity(const std::string &name);
  EntityID createEntityWithUUID(EntityUUID uuid, const std::string &name);
  void destroyEntity(EntityID e);
  void clear();

  bool isAlive(EntityID e) const;

  const std::vector<EntityID> &alive() const { return m_alive; }
  std::vector<EntityID> roots() const;

  // ---- Hierarchy ----
  CHierarchy &hierarchy(EntityID e);
  const CHierarchy &hierarchy(EntityID e) const;

  EntityID parentOf(EntityID e) const;
  void setParent(EntityID child, EntityID newParent);
  void setParentKeepWorld(EntityID child, EntityID newParent);
  EntityID cloneSubtree(EntityID root, EntityID newParent);

  // ---- UUID ----
  EntityUUID uuid(EntityID e) const;
  EntityID findByUUID(EntityUUID uuid) const;
  void setUUIDSeed(uint64_t seed);
  uint64_t uuidSeed() const { return m_uuidGen.seed(); }

  // ---- Name ----
  CName &name(EntityID e);
  const CName &name(EntityID e) const;
  void setName(EntityID e, const std::string &n);

  // ---- Transform ----
  CTransform &transform(EntityID e);
  const CTransform &transform(EntityID e) const;

  CWorldTransform &worldTransform(EntityID e);
  const CWorldTransform &worldTransform(EntityID e) const;

  // recompute world matrices if dirty (hierarchy-aware)
  void updateTransforms();

  // ---- Mesh ----
  bool hasMesh(EntityID e) const;
  CMesh &ensureMesh(EntityID e);
  CMesh &mesh(EntityID e);
  const CMesh &mesh(EntityID e) const;

  uint32_t submeshCount(EntityID e) const;
  MeshSubmesh &submesh(EntityID e, uint32_t si);

  // ---- Camera ----
  bool hasCamera(EntityID e) const;
  CCamera &ensureCamera(EntityID e);
  CCamera &camera(EntityID e);
  const CCamera &camera(EntityID e) const;

  CCameraMatrices &cameraMatrices(EntityID e);
  const CCameraMatrices &cameraMatrices(EntityID e) const;

  // ---- Light ----
  bool hasLight(EntityID e) const;
  CLight &ensureLight(EntityID e);
  CLight &light(EntityID e);
  const CLight &light(EntityID e) const;

  // Active camera (only ONE)
  EntityID activeCamera() const { return m_activeCamera; }
  void setActiveCamera(EntityID cam);
  EntityUUID activeCameraUUID() const { return uuid(m_activeCamera); }
  void setActiveCameraUUID(EntityUUID id);

private:
  // Entity storage (simple SoA-by-map for now; you can swap to sparse sets
  // later)
  EntityID m_next = {1, 0};
  std::vector<EntityID> m_alive;

  // Core components
  std::unordered_map<EntityID, CHierarchy, EntityHash> m_hier;
  std::unordered_map<EntityID, CName, EntityHash> m_name;
  std::unordered_map<EntityID, CTransform, EntityHash> m_tr;
  std::unordered_map<EntityID, CWorldTransform, EntityHash> m_wtr;

  // Optional components
  std::unordered_map<EntityID, CMesh, EntityHash> m_mesh;
  std::unordered_map<EntityID, CCamera, EntityHash> m_cam;
  std::unordered_map<EntityID, CCameraMatrices, EntityHash> m_camMat;
  std::unordered_map<EntityID, CLight, EntityHash> m_light;

  // UUID storage
  EntityUUIDGen m_uuidGen{};
  std::unordered_map<EntityID, EntityUUID, EntityHash> m_uuid;
  std::unordered_map<uint64_t, EntityID> m_entityByUUID;

  // World meta
  EntityID m_activeCamera = InvalidEntity;

  // Events
  WorldEvents m_events;

private:
  void detachFromParent(EntityID child);
  void attachToParent(EntityID child, EntityID newParent);
  void destroySubtree(EntityID root);

  // Transform helpers
  glm::mat4 localMatrix(EntityID e) const;
  void markWorldDirtyRecursive(EntityID e);
};

} // namespace Nyx
