#pragma once

#include "../light/ShadowData.h"
#include "../../scene/World.h"
#include <vector>
#include <glm/glm.hpp>

namespace Nyx {

/**
 * @brief Manages GPU-resident light information buffer.
 *
 * Populates GPULightInfo array from world entities, manages shadow metadata,
 * and provides data to forward rendering passes.
 */
class LightInfoBuffer final {
public:
  struct UpdateResult {
    std::vector<uint64_t> csmLightKeys;      // Directional lights with CSM
    std::vector<uint64_t> spotLightKeys;     // Spot lights with shadows
    std::vector<uint64_t> pointLightKeys;    // Point lights with shadows
    std::vector<uint64_t> dirLightKeys;      // Additional directional lights with shadows

    std::vector<GPULightInfo> lightData;                   // GPU light buffer
    std::vector<ShadowMetadataCSM> csmMetadata;            // CSM cascade data
    std::vector<ShadowMetadataSpot> spotMetadata;          // Spot light data
    std::vector<ShadowMetadataPoint> pointMetadata;        // Point light data
    std::vector<ShadowMetadataDir> dirMetadata;            // Dir light data
  };

  /**
   * @brief Query world for all lights and prepare GPU buffers.
   */
  UpdateResult update(const World &world);

  /**
   * @brief Get the light count from last update.
   */
  uint32_t getLightCount() const { return m_lightCount; }

private:
  uint32_t m_lightCount = 0;
  UpdateResult m_lastResult;
};

} // namespace Nyx
