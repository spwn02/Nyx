#pragma once

#include "EntityID.h"
#include <cstdint>

namespace Nyx {

// 32-bit pick id layout
// bits 0..19: entity (up to 1,048,575 entities)
// bits 20..27: sub-entity (up to 256 sub-entities per entity)
// bits 28..31: kind/flags (TODO: primitive, bone, etc)
// For now: kind=0 (mesh/submesh), still reserved.

inline constexpr uint32_t Pick_EntityBits = 20u;
inline constexpr uint32_t Pick_SubmeshBits = 8u;

inline constexpr uint32_t Pick_EntityMask = (1u << Pick_EntityBits) - 1u;
inline constexpr uint32_t Pick_SubmeshMask = (1u << Pick_SubmeshBits) - 1u;

struct PickKey {
  EntityID entity = InvalidEntity;
  uint32_t submesh = 0; // 0..255
};

inline uint32_t packPick(EntityID e, uint32_t submesh) {
  const uint32_t ee = static_cast<uint32_t>(e) & Pick_EntityMask;
  const uint32_t ss = (submesh & Pick_SubmeshMask) << Pick_EntityBits;
  return ss | ee;
}

inline PickKey unpackPick(uint32_t pick) {
  PickKey k;
  k.entity = static_cast<EntityID>(pick & Pick_EntityMask);
  k.submesh = (pick >> Pick_EntityBits) & Pick_SubmeshMask;
  return k;
}

inline EntityID pickEntity(uint32_t pick) {
  return static_cast<EntityID>(pick & Pick_EntityMask);
}

inline uint32_t pickSubmesh(uint32_t pick) {
  return (pick >> Pick_EntityBits) & Pick_SubmeshMask;
}

} // namespace Nyx
