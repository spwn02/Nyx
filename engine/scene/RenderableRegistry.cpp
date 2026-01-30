#include "RenderableRegistry.h"

#include "scene/Pick.h"
#include "scene/World.h"
#include "scene/WorldEvents.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace Nyx {

static glm::mat4 renderModelForEntity(const World &world, EntityID e) {
  glm::mat4 W = world.worldTransform(e).world;
  if (world.hasCamera(e)) {
    const glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(0.2f));
    W = W * S;
  }
  return W;
}

static void applyLightFields(const World &world, EntityID e, Renderable &r) {
  if (!world.hasLight(e)) {
    r.isLight = false;
    return;
  }
  const auto &L = world.light(e);
  r.isLight = true;
  r.lightColor = L.color;
  r.lightIntensity = L.intensity;
  r.lightExposure = L.exposure;
}

static void applyCameraFields(const World &world, EntityID e, Renderable &r) {
  r.isCamera = world.hasCamera(e);
}

void RenderableRegistry::clear() {
  m_items.clear();
  m_pickToIndex.clear();
  m_entityToIndices.clear();
}

bool RenderableRegistry::hasEntity(EntityID e) const {
  auto it = m_entityToIndices.find(e);
  return it != m_entityToIndices.end() && !it->second.empty();
}

uint32_t RenderableRegistry::submeshCount(EntityID e) const {
  auto it = m_entityToIndices.find(e);
  if (it == m_entityToIndices.end())
    return 0;
  return (uint32_t)it->second.size();
}

const Renderable *RenderableRegistry::findByPick(uint32_t pickID) const {
  auto it = m_pickToIndex.find(pickID);
  if (it == m_pickToIndex.end())
    return nullptr;
  const uint32_t idx = it->second;
  if (idx >= m_items.size())
    return nullptr;
  return &m_items[idx];
}

void RenderableRegistry::rebuildAll(const World &world) {
  clear();
  m_items.reserve(world.alive().size());

  std::vector<EntityID> ents = world.alive();
  std::sort(ents.begin(), ents.end());

  for (EntityID e : ents) {
    if (!world.hasMesh(e))
      continue;

    const uint32_t n = world.submeshCount(e);
    if (n == 0)
      continue;

    for (uint32_t si = 0; si < n; ++si) {
      const auto &sm = world.mesh(e).submeshes[(size_t)si];

      Renderable r{};
      r.entity = e;
      r.submesh = si;
      r.pickID = packPick(e, si);
      r.mesh = sm.type;
      r.model = renderModelForEntity(world, e);
      applyLightFields(world, e, r);
      applyCameraFields(world, e, r);

      // materialGpuIndex is engine-owned; registry doesn’t decide it.
      r.materialGpuIndex = 0;

      m_items.push_back(r);
    }
  }

  rebuildMaps();
}

void RenderableRegistry::rebuildMaps() {
  m_pickToIndex.clear();
  m_entityToIndices.clear();

  for (uint32_t i = 0; i < (uint32_t)m_items.size(); ++i) {
    indexRenderable(i);
  }
}

void RenderableRegistry::indexRenderable(uint32_t idx) {
  if (idx >= m_items.size())
    return;
  const Renderable &r = m_items[idx];
  m_pickToIndex[r.pickID] = idx;
  m_entityToIndices[r.entity].push_back(idx);
}

void RenderableRegistry::eraseIndex(uint32_t idx) {
  if (idx >= m_items.size())
    return;

  const uint32_t last = (uint32_t)m_items.size() - 1u;
  const Renderable removed = m_items[idx];

  // Remove pick mapping for the removed renderable
  m_pickToIndex.erase(removed.pickID);

  // Remove idx from entity indices list (swap-erase inside the list)
  {
    auto it = m_entityToIndices.find(removed.entity);
    if (it != m_entityToIndices.end()) {
      auto &lst = it->second;
      for (size_t k = 0; k < lst.size(); ++k) {
        if (lst[k] == idx) {
          lst[k] = lst.back();
          lst.pop_back();
          break;
        }
      }
      if (lst.empty())
        m_entityToIndices.erase(it);
    }
  }

  if (idx != last) {
    // Move last into idx
    m_items[idx] = m_items[last];

    // Fix moved item mappings:
    const Renderable &moved = m_items[idx];

    // Update pick -> new idx
    m_pickToIndex[moved.pickID] = idx;

    // Update entity->indices list: replace "last" with "idx"
    auto it = m_entityToIndices.find(moved.entity);
    if (it != m_entityToIndices.end()) {
      auto &lst = it->second;
      for (uint32_t &v : lst) {
        if (v == last) {
          v = idx;
          break;
        }
      }
    }
  }

  m_items.pop_back();
}

void RenderableRegistry::removeEntity(EntityID e) {
  // Erase all renderables owned by entity e.
  // We keep it robust even if indices change during erase.
  while (true) {
    auto it = m_entityToIndices.find(e);
    if (it == m_entityToIndices.end() || it->second.empty())
      break;
    const uint32_t idx = it->second.back();
    eraseIndex(idx);
  }
}

void RenderableRegistry::updateEntityTransform(const World &world, EntityID e) {
  auto it = m_entityToIndices.find(e);
  if (it == m_entityToIndices.end())
    return;

  const glm::mat4 W = renderModelForEntity(world, e);
  for (uint32_t idx : it->second) {
    if (idx < m_items.size()) {
      m_items[idx].model = W;
      applyLightFields(world, e, m_items[idx]);
      applyCameraFields(world, e, m_items[idx]);
    }
  }
}

void RenderableRegistry::rebuildEntity(const World &world, EntityID e) {
  removeEntity(e);

  if (!world.isAlive(e))
    return;
  if (!world.hasMesh(e))
    return;

  const uint32_t n = world.submeshCount(e);
  if (n == 0)
    return;

  // Append in submesh order for determinism (relative to itself)
  const glm::mat4 W = renderModelForEntity(world, e);

  for (uint32_t si = 0; si < n; ++si) {
    const auto &sm = world.mesh(e).submeshes[(size_t)si];

    Renderable r{};
    r.entity = e;
    r.submesh = si;
    r.pickID = packPick(e, si);
    r.mesh = sm.type;
    r.model = W;
    r.materialGpuIndex = 0;
    applyLightFields(world, e, r);
    applyCameraFields(world, e, r);

    m_items.push_back(r);
    indexRenderable((uint32_t)m_items.size() - 1u);
  }
}

void RenderableRegistry::applyEvents(const World &world,
                                     const WorldEvents &ev) {
  // We expect caller to run world.updateTransforms() before this,
  // or at least before rendering. But we still handle conservatively.

  // Collect a minimal set of entity ops (avoid doing the same work 10x)
  std::vector<EntityID> needRebuild;
  std::vector<EntityID> needXform;
  std::vector<EntityID> needRemove;

  auto pushUnique = [](std::vector<EntityID> &v, EntityID e) {
    if (e == InvalidEntity)
      return;
    if (std::find(v.begin(), v.end(), e) == v.end())
      v.push_back(e);
  };

  for (const WorldEvent &e : ev.events()) {
    switch (e.type) {
    case WorldEventType::EntityCreated:
      // Might or might not have mesh; rebuild is safe once.
      pushUnique(needRebuild, e.a);
      break;

    case WorldEventType::EntityDestroyed:
      pushUnique(needRemove, e.a);
      break;

    case WorldEventType::MeshChanged:
      pushUnique(needRebuild, e.a);
      break;

    case WorldEventType::TransformChanged:
      pushUnique(needXform, e.a);
      break;

    case WorldEventType::LightChanged:
      // Light gizmo coloration/intensity lives on renderable fields.
      pushUnique(needXform, e.a);
      break;

    case WorldEventType::ParentChanged:
      // child local changed; easiest: transform update (world should mark dirty
      // recursively anyway)
      pushUnique(needXform, e.a);
      break;

    default:
      break;
    }
  }

  // Deterministic processing order
  std::sort(needRemove.begin(), needRemove.end());
  std::sort(needRebuild.begin(), needRebuild.end());
  std::sort(needXform.begin(), needXform.end());

  // Removes first
  for (EntityID e : needRemove) {
    removeEntity(e);
  }

  // Rebuild (also sets correct model)
  for (EntityID e : needRebuild) {
    rebuildEntity(world, e);
  }

  // Transform-only updates (skip ones we rebuilt)
  for (EntityID e : needXform) {
    if (std::binary_search(needRebuild.begin(), needRebuild.end(), e))
      continue;
    updateEntityTransform(world, e);
  }
}

} // namespace Nyx
