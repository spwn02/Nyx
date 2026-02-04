#pragma once

#include <glm/glm.hpp>
#include <stdint.h>

namespace Nyx {

// ============================================================================
// Shadow metadata for a single light - packed for GPU UBO/SSBO
// ============================================================================

struct ShadowMetadataCSM {
  // For directional lights with cascades
  // [cascade][2] = {uvMin, uvMax}
  glm::vec4 atlasUVBounds[4]; // 4 cascades
  float splitDepths[4];       // Cascade split depths (linear)
  glm::mat4 viewProj[4];      // Per-cascade view-projection
};

struct ShadowMetadataSpot {
  // For spot lights
  glm::vec4 atlasUVMin;       // Atlas region bounds
  glm::vec4 atlasUVMax;
  glm::mat4 viewProj;         // Spot light VP
  float pcfRadius;
  float slopeBias;
  float normalBias;
  float _pad0;
};

struct ShadowMetadataPoint {
  // For point lights
  // Stores cubemap array index and metadata
  uint32_t cubemapArrayIndex; // Which element in cubemap array
  float pcfRadius;
  float slopeBias;
  float normalBias;
  glm::vec3 worldPos;
  float farPlane;
};

struct ShadowMetadataDir {
  // For additional directional lights (non-cascaded)
  glm::vec4 atlasUVMin;       // Simple shadow map in atlas
  glm::vec4 atlasUVMax;
  glm::mat4 viewProj;
  float pcfRadius;
  float slopeBias;
  float normalBias;
  float _pad0;
};

// ============================================================================
// Light info packed for GPU buffer
// ============================================================================

struct GPULightInfo {
  glm::vec3 position;         // World position (for point/spot) or direction (for directional)
  uint32_t type;              // LightType enum

  glm::vec3 color;
  float intensity;

  glm::vec3 direction;        // For spot/directional lights (normalized)
  float radius;               // For point/spot attenuation

  float innerAngle;           // For spot lights (radians)
  float outerAngle;           // For spot lights (radians)
  uint32_t castShadow;        // bool as uint32
  uint32_t shadowMetadataIdx; // Index into shadow metadata buffer (-1 if no shadow)

  // Padding to 256 bytes per light (useful for GPU alignment)
  glm::vec4 _padding[12];
};

static_assert(sizeof(GPULightInfo) == 256, "GPULightInfo must be 256 bytes for GPU alignment");

// ============================================================================
// Shadow metadata offsets in buffer
// ============================================================================

struct ShadowMetadataBuffer {
  // Layout:
  // - [0 .. N_DIR_CSM-1]:     ShadowMetadataCSM (for dir lights with cascades)
  // - [N_DIR_CSM .. N_DIR]:   ShadowMetadataDir (for additional dir lights)
  // - [N_DIR .. N_DIR+N_SPOT]: ShadowMetadataSpot (for spot lights)
  // - [N_DIR+N_SPOT .. END]:  ShadowMetadataPoint (for point lights)
  
  // Offsets are stored in light info buffer's shadowMetadataIdx
  // High byte = metadata type (0=CSM, 1=Dir, 2=Spot, 3=Point)
  // Low 24 bits = index within that type

  static constexpr uint32_t TYPE_MASK = 0xFF000000;
  static constexpr uint32_t INDEX_MASK = 0x00FFFFFF;

  static constexpr uint32_t TYPE_CSM = 0;
  static constexpr uint32_t TYPE_DIR = 1;
  static constexpr uint32_t TYPE_SPOT = 2;
  static constexpr uint32_t TYPE_POINT = 3;

  static uint32_t packMetadataIdx(uint32_t type, uint32_t index) {
    return ((type & 0xFF) << 24) | (index & INDEX_MASK);
  }

  static uint32_t unpackType(uint32_t packed) {
    return (packed >> 24) & 0xFF;
  }

  static uint32_t unpackIndex(uint32_t packed) {
    return packed & INDEX_MASK;
  }
};

} // namespace Nyx
