#pragma once

#include "EntityID.h"
#include <cstdint>
#include <vector>

namespace Nyx {

enum class WorldEventType : uint8_t {
  None = 0,

  EntityCreated,
  EntityDestroyed,

  ParentChanged,     // a: child, b: newParent, c: oldParent
  NameChanged,       // a: entity
  TransformChanged,  // a: entity
  MeshChanged,       // a: entity

  CameraCreated,     // a: camera entity
  CameraDestroyed,   // a: camera entity
  ActiveCameraChanged, // a: newActive, b: oldActive
  LightChanged        // a: entity
};

struct WorldEvent final {
  WorldEventType type = WorldEventType::None;
  EntityID a = InvalidEntity;
  EntityID b = InvalidEntity;
  EntityID c = InvalidEntity;
  uint32_t u0 = 0;
  uint32_t u1 = 0;
};

class WorldEvents final {
public:
  void clear() { m_events.clear(); }

  void push(const WorldEvent& e) { m_events.push_back(e); }

  const std::vector<WorldEvent>& events() const { return m_events; }
  bool empty() const { return m_events.empty(); }

private:
  std::vector<WorldEvent> m_events;
};

} // namespace Nyx
