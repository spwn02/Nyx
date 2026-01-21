#pragma once

#include "../scene/Pick.h"
#include "../scene/World.h"
#include <vector>

namespace Nyx {

inline uint32_t submeshCountFor(World &w, EntityID e) {
  // Phase-2A: procedural meshes: 1 submesh
  // Later: model component provides true count.
  if (w.hasMesh(e))
    return 1;
  return 0;
}

inline void appendEntitySubmeshes(World &w, EntityID e,
                                  std::vector<uint32_t> &out) {
  const uint32_t n = submeshCountFor(w, e);
  for (uint32_t s = 0; s < n; ++s)
    out.push_back(packPick(e, s));
}

inline void collectCycleList(World &w, EntityID root,
                             std::vector<uint32_t> &out) {
  out.clear();
  if (!w.isAlive(root))
    return;

  appendEntitySubmeshes(w, root, out);

  EntityID c = w.hierarchy(root).firstChild;
  while (c != InvalidEntity) {
    EntityID next = w.hierarchy(c).nextSibling;

    // include child itself (its submeshes)
    appendEntitySubmeshes(w, c, out);

    // also include grandchildren? (Blender-like: cycling usually cycles leaf
    // hits, but you asked specifically "submesh/child sequentially". We'll
    // include only direct children for now.)
    c = next;
  }
}

} // namespace Nyx
