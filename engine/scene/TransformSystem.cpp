#include "TransformSystem.h"
#include "../core/Assert.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include "World.h"

namespace Nyx {

static glm::mat4 trs(const CTransform &t) {
  glm::mat4 M(1.0f);
  M = glm::translate(M, t.translation);
  M *= glm::toMat4(t.rotation);
  M = glm::scale(M, t.scale);
  return M;
}

static void markSubtreeDirty(World &w, EntityID e) {
  if (!w.isAlive(e))
    return;
  if (w.hasWorldTransform(e))
    w.worldTransform(e).dirty = true;

  EntityID c = w.hierarchy(e).firstChild;
  while (c != InvalidEntity) {
    EntityID next = w.hierarchy(c).nextSibling;
    markSubtreeDirty(w, c);
    c = next;
  }
}

static void updateNode(World &w, EntityID e, const glm::mat4 &parentWorld,
                       bool parentDirty) {
  auto &lt = w.transform(e);
  auto &wt = w.worldTransform(e);

  // If parent moved or local dirty, mark world dirty (cheap)
  if (lt.dirty || parentDirty)
    wt.dirty = true;

  const bool worldUpdated = wt.dirty;
  if (wt.dirty) {
    wt.world = parentWorld * trs(lt);
    wt.dirty = false;
    lt.dirty = false;
  }

  EntityID c = w.hierarchy(e).firstChild;
  while (c != InvalidEntity) {
    EntityID next = w.hierarchy(c).nextSibling;
    updateNode(w, c, wt.world, worldUpdated);
    c = next;
  }
}

void TransformSystem::update(World &world) {
  // Roots drive traversal
  auto roots = world.roots();
  for (EntityID r : roots) {
    if (!world.isAlive(r))
      continue;
    updateNode(world, r, glm::mat4(1.0f), false);
  }
}

} // namespace Nyx
