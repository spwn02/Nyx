#pragma once

#include "World.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace Nyx {

// A cycle target is always a submesh pick (entity + submeshIndex).
struct CycleTarget final {
  EntityID entity = InvalidEntity;
  uint32_t submesh = 0;
};

// Build a deterministic list: clicked entity submeshes first,
// then each direct child entity submeshes (in sibling order).
inline void buildCycleTargets(World &world, EntityID clicked,
                              std::vector<CycleTarget> &outTargets,
                              bool includeChildren = true) {
  outTargets.clear();

  if (!world.isAlive(clicked) || !world.hasMesh(clicked))
    return;

  const uint32_t n0 = world.submeshCount(clicked);
  for (uint32_t i = 0; i < n0; ++i)
    outTargets.push_back({clicked, i});

  if (!includeChildren)
    return;

  EntityID ch = world.hierarchy(clicked).firstChild;
  while (ch != InvalidEntity) {
    if (world.isAlive(ch) && world.hasMesh(ch)) {
      const uint32_t nc = world.submeshCount(ch);
      for (uint32_t i = 0; i < nc; ++i)
        outTargets.push_back({ch, i});
    }
    ch = world.hierarchy(ch).nextSibling;
  }
}

// Cycle index storage: exactly your map (entity -> idx).
// Returns the next packed pick id using provided packPick(entity, submesh).
inline uint32_t cycleNextPickForEntity(
    World &world, EntityID clicked,
    std::unordered_map<EntityID, uint32_t, EntityHash> &cycleIndexByEntity,
    const std::function<uint32_t(EntityID, uint32_t)> &packPick,
    bool includeChildren = true) {

  std::vector<CycleTarget> targets;
  buildCycleTargets(world, clicked, targets, includeChildren);
  if (targets.empty())
    return 0;

  uint32_t &idx = cycleIndexByEntity[clicked];
  if (idx >= (uint32_t)targets.size())
    idx = 0;

  const CycleTarget t = targets[idx];

  // Advance for next click
  idx = (idx + 1u) % (uint32_t)targets.size();

  return packPick(t.entity, t.submesh);
}

} // namespace Nyx
