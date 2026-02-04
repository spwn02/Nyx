// Sky/Env constants UBO
// Binding contract: binding = 2
layout(std140, binding = 2) uniform SkyUBO
{
  mat4 uInvViewProj;   // for sky ray reconstruction
  vec4 uCamPos;        // xyz = camera world pos, w unused
  vec4 uSkyParams;     // x=intensity, y=exposureStops, z=rotationYawRad, w=drawBackground(0/1)
} gSky;
