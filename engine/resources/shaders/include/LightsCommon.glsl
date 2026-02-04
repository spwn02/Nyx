// Light types
const uint LIGHT_DIR  = 0u;
const uint LIGHT_POINT= 1u;
const uint LIGHT_SPOT = 2u;

// std430-friendly packed light
// Notes:
// - position.w = radius (for point/spot), unused for dir
// - direction.xyz = normalized direction (world), direction.w = cosOuter
// - params.x = cosInner, params.y = intensity, params.z = type, params.w = castShadow
// - shadowData.x = metadata index, yzw = reserved
struct GpuLight {
  vec4 color;      // rgb=color (linear), a unused
  vec4 position;   // xyz=pos, w=radius
  vec4 direction;  // xyz=dir, w=cosOuter
  vec4 params;     // x=cosInner, y=intensity, z=type (0/1/2), w=castShadow
  vec4 shadowData; // x=shadowMetadataIdx, yzw=reserved
};

layout(std430, binding = 20) readonly buffer LightsSSBO {
  uint  uLightCount;
  uint  _pad0;
  uint  _pad1;
  uint  _pad2;
  GpuLight gLights[];
};
