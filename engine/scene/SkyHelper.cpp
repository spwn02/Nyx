#include "SkyHelper.h"

#include "World.h"

namespace Nyx {

EntityID getSkyEntity(World &world) {
  // Look for existing Sky component
  for (EntityID e : world.alive()) {
    if (!world.isAlive(e))
      continue;
    if (world.hasSky(e)) {
      return e;
    }
  }

  // Create if missing
  EntityID skyE = world.createEntity("Sky");
  world.ensureSky(skyE);
  return skyE;
}

} // namespace Nyx
