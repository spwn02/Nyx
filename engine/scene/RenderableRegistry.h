#pragma once

#include "scene/EntityID.h"
#include "scene/Renderable.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace Nyx {

class World;
class WorldEvents;

class RenderableRegistry final {
public:
  void clear();
  bool empty() const { return m_items.empty(); }

  // Full rebuild (call once at scene load or if you want brute-force reset)
  void rebuildAll(const World &world);

  // Incremental update using WorldEvents (main feature)
  void applyEvents(const World &world, const WorldEvents &ev);

  // Read access (renderer iterates this)
  const std::vector<Renderable> &all() const { return m_items; }

  // Helpers
  bool hasEntity(EntityID e) const;
  uint32_t submeshCount(EntityID e) const; // from cached mapping
  const Renderable *findByPick(uint32_t pickID) const;

private:
  std::vector<Renderable> m_items;

  // Fast lookups
  std::unordered_map<uint32_t, uint32_t>
      m_pickToIndex; // pickID -> index in m_items
  std::unordered_map<EntityID, std::vector<uint32_t>, EntityHash>
      m_entityToIndices;

  // Internal ops
  void removeEntity(EntityID e);
  void rebuildEntity(const World &world, EntityID e);
  void updateEntityTransform(const World &world, EntityID e);
  void rebuildMaps(); // (rare) safety fallback

  void eraseIndex(uint32_t idx);
  void indexRenderable(uint32_t idx);
};

} // namespace Nyx
