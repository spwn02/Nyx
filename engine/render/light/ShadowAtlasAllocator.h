// ===============================================
// File: render/light/ShadowAtlasAllocator.h
// ===============================================
#pragma once
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Nyx {

struct ShadowTile final {
  uint16_t x = 0, y = 0;
  uint16_t size = 0;     // inner size (without guard)
  uint16_t guard = 0;    // texels
  uint16_t atlasW = 1, atlasH = 1;

  // inner rect in pixels
  uint16_t ix() const { return (uint16_t)(x + guard); }
  uint16_t iy() const { return (uint16_t)(y + guard); }
  uint16_t iw() const { return size; }
  uint16_t ih() const { return size; }

  // normalized atlas UVs for inner rect
  void uvScaleBias(float &sx, float &sy, float &bx, float &by) const {
    sx = float(iw()) / float(atlasW);
    sy = float(ih()) / float(atlasH);
    bx = float(ix()) / float(atlasW);
    by = float(iy()) / float(atlasH);
  }
  void uvClamp(float &u0, float &v0, float &u1, float &v1) const {
    u0 = float(ix() + 1) / float(atlasW);
    v0 = float(iy() + 1) / float(atlasH);
    u1 = float(ix() + iw() - 1) / float(atlasW);
    v1 = float(iy() + ih() - 1) / float(atlasH);
  }
};

class ShadowAtlasAllocator final {
public:
  void reset(uint16_t atlasW, uint16_t atlasH) {
    m_atlasW = (atlasW == 0) ? 1u : atlasW;
    m_atlasH = (atlasH == 0) ? 1u : atlasH;
    m_shelves.clear();
    m_used.clear();
    m_free.clear();
  }

  // Stable allocation by key; if exists, returns same tile.
  ShadowTile acquire(uint64_t key, uint16_t innerSize, uint16_t guard) {
    innerSize = (innerSize == 0) ? 1u : innerSize;
    guard = (uint16_t)((guard > 32) ? 32 : guard);

    auto it = m_used.find(key);
    if (it != m_used.end())
      return it->second;

    // try reuse from free pool (exact match)
    for (size_t i = 0; i < m_free.size(); ++i) {
      if (m_free[i].size == innerSize && m_free[i].guard == guard) {
        ShadowTile t = m_free[i];
        m_free.erase(m_free.begin() + (ptrdiff_t)i);
        m_used[key] = t;
        return t;
      }
    }

    ShadowTile t{};
    if (!allocNew(innerSize, guard, t)) {
      // fallback: smallest possible
      allocNew(1, guard, t);
    }
    m_used[key] = t;
    return t;
  }

  void endFrameAndRecycleUnused(const std::vector<uint64_t> &aliveKeys) {
    // mark alive
    std::unordered_map<uint64_t, bool> alive;
    alive.reserve(aliveKeys.size());
    for (uint64_t k : aliveKeys) alive[k] = true;

    for (auto it = m_used.begin(); it != m_used.end();) {
      if (alive.find(it->first) == alive.end()) {
        m_free.push_back(it->second);
        it = m_used.erase(it);
      } else {
        ++it;
      }
    }
  }

  uint16_t atlasW() const { return m_atlasW; }
  uint16_t atlasH() const { return m_atlasH; }

private:
  struct Shelf final {
    uint16_t y = 0;
    uint16_t h = 0;
    uint16_t x = 0;
  };

  uint16_t m_atlasW = 1, m_atlasH = 1;
  std::vector<Shelf> m_shelves;
  std::unordered_map<uint64_t, ShadowTile> m_used;
  std::vector<ShadowTile> m_free;

  bool allocNew(uint16_t innerSize, uint16_t guard, ShadowTile &out) {
    const uint16_t outer = (uint16_t)(innerSize + 2u * guard);
    if (outer > m_atlasW || outer > m_atlasH)
      return false;

    // first-fit shelf
    for (auto &s : m_shelves) {
      if (s.h >= outer && (uint32_t)s.x + outer <= m_atlasW) {
        out.x = s.x;
        out.y = s.y;
        out.size = innerSize;
        out.guard = guard;
        out.atlasW = m_atlasW;
        out.atlasH = m_atlasH;
        s.x = (uint16_t)(s.x + outer);
        return true;
      }
    }

    // new shelf
    uint16_t y = 0;
    for (const auto &s : m_shelves)
      y = (uint16_t)std::max<uint32_t>(y, (uint32_t)s.y + s.h);

    if ((uint32_t)y + outer > m_atlasH)
      return false;

    Shelf s{};
    s.y = y;
    s.h = outer;
    s.x = outer;

    out.x = 0;
    out.y = y;
    out.size = innerSize;
    out.guard = guard;
    out.atlasW = m_atlasW;
    out.atlasH = m_atlasH;

    m_shelves.push_back(s);
    return true;
  }
};

// ============================================================================
// Specialized allocators for spot and directional shadow atlases
// ============================================================================

class SpotShadowAtlasAllocator final {
  // Packs spot light shadows (single map per light) into atlas
  ShadowAtlasAllocator m_allocator;

public:
  void reset(uint16_t atlasW, uint16_t atlasH) {
    m_allocator.reset(atlasW, atlasH);
  }

  ShadowTile acquire(uint64_t lightKey, uint16_t shadowRes, uint16_t guard = 4) {
    return m_allocator.acquire(lightKey, shadowRes, guard);
  }

  void endFrameAndRecycleUnused(const std::vector<uint64_t> &aliveSpotLightKeys) {
    m_allocator.endFrameAndRecycleUnused(aliveSpotLightKeys);
  }

  uint16_t atlasW() const { return m_allocator.atlasW(); }
  uint16_t atlasH() const { return m_allocator.atlasH(); }
};

class DirShadowAtlasAllocator final {
  // Packs non-primary directional lights (single map per light) into atlas
  ShadowAtlasAllocator m_allocator;

public:
  void reset(uint16_t atlasW, uint16_t atlasH) {
    m_allocator.reset(atlasW, atlasH);
  }

  ShadowTile acquire(uint64_t lightKey, uint16_t shadowRes, uint16_t guard = 4) {
    return m_allocator.acquire(lightKey, shadowRes, guard);
  }

  void endFrameAndRecycleUnused(const std::vector<uint64_t> &aliveDirLightKeys) {
    m_allocator.endFrameAndRecycleUnused(aliveDirLightKeys);
  }

  uint16_t atlasW() const { return m_allocator.atlasW(); }
  uint16_t atlasH() const { return m_allocator.atlasH(); }
};

} // namespace Nyx
