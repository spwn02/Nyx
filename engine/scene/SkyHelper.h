#pragma once

#include "scene/EntityID.h"

namespace Nyx {

class World;

// Get or create the singleton Sky entity
EntityID getSkyEntity(World &world);

} // namespace Nyx
