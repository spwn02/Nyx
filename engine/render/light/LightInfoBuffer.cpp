#include "LightInfoBuffer.h"
#include "../../scene/Components.h"
#include "../../scene/EntityID.h"

namespace Nyx {

LightInfoBuffer::UpdateResult LightInfoBuffer::update(const World &world) {
  UpdateResult result;

  // TODO: Iterate through world entities
  // For each entity with CLight component:
  //   1. Create GPULightInfo entry
  //   2. Classify by type (directional/spot/point)
  //   3. If castShadow, add to appropriate key list and metadata list
  //   4. Pack metadata index into GPULightInfo

  // Placeholder implementation
  m_lightCount = 0;

  return result;
}

} // namespace Nyx
