#pragma once

namespace Nyx {

class World;

class TransformSystem final {
public:
  void update(World &world); // recompute all dirty
};

} // namespace Nyx
