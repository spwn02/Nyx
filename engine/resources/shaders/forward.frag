#version 460

layout(location = 0) out vec4 oColor;
layout(location = 1) out uint oID;

uniform uint u_PickID;
uniform uint u_MaterialIndex;
uniform uint u_ViewMode;
uniform int u_IsLight;
uniform vec4 u_LightColorIntensity; // rgb=color, a=intensity
uniform float u_LightExposure;
layout(binding = 6) uniform sampler2DArray uShadowDir;
layout(binding = 7) uniform sampler2DArray uShadowSpot;
layout(binding = 8) uniform samplerCubeArray uShadowPoint;
uniform vec4 u_CSM_Splits;
uniform vec2 u_ShadowDirSize;
uniform vec2 u_ShadowSpotSize;

layout(binding = 20) uniform samplerCube u_EnvIrradiance;
layout(binding = 21) uniform samplerCube u_EnvPrefilter;
layout(binding = 22) uniform sampler2D u_BRDFLUT;
uniform int u_HasIBL;

in vec3 vN;
in vec3 vP;
in vec3 vPV;

struct GpuMaterialPacked {
  vec4 baseColor;
  vec4 mrAoCut; // x=metallic y=roughness z=ao w=alphaCutoff
  vec4 flags;   // reserved
};

layout(std430, binding = 14) readonly buffer MaterialsSSBO {
  GpuMaterialPacked mats[];
}
gMats;

struct GpuLight {
  vec4 posRadius;
  vec4 dirType;
  vec4 colorIntensity;
  vec4 spotParams;
  uvec4 shadow;
};

layout(std430, binding = 16) readonly buffer LightsSSBO {
  uvec4 header;
  GpuLight lights[];
}
gLights;

layout(std430, binding = 17) readonly buffer ShadowDirMatrices {
  mat4 dirVP[];
}
gShadowDir;

layout(std430, binding = 18) readonly buffer ShadowSpotMatrices {
  mat4 spotVP[];
}
gShadowSpot;

#include "include/common.glsl"
#include "include/forward_light.glsl"
#include "include/SkyCommon.glsl"

void main() {
  if (u_ViewMode == 7u) {
    uint id = u_PickID;
    vec3 c = vec3(float((id) & 255u) / 255.0, float((id >> 8u) & 255u) / 255.0,
                  float((id >> 16u) & 255u) / 255.0);
    oColor = vec4(c, 1.0);
    oID = u_PickID;
    return;
  }

  GpuMaterialPacked m = gMats.mats[u_MaterialIndex];

  if (u_IsLight != 0) {
    float intensity = u_LightColorIntensity.a;
    if (u_LightExposure != 0.0) {
      intensity *= exp2(u_LightExposure);
    }
    vec3 lightRgb = u_LightColorIntensity.rgb * intensity;
    oColor = vec4(lightRgb, 1.0);
    oID = u_PickID;
    return;
  }

  vec3 baseColor = m.baseColor.rgb;

  if (u_ViewMode == 1u) {
    oColor = vec4(baseColor, 1.0);
    oID = u_PickID;
    return;
  }

  float metallic = clamp(m.mrAoCut.x, 0.0, 1.0);
  float roughness = clamp(m.mrAoCut.y, 0.02, 1.0);

  if (u_ViewMode == 3u) {
    oColor = vec4(vec3(roughness), 1.0);
    oID = u_PickID;
    return;
  }

  if (u_ViewMode == 4u) {
    oColor = vec4(vec3(metallic), 1.0);
    oID = u_PickID;
    return;
  }

  float ao = clamp(m.mrAoCut.z, 0.0, 1.0);

  if (u_ViewMode == 5u) {
    oColor = vec4(vec3(ao), 1.0);
    oID = u_PickID;
    return;
  }

  vec3 N = normalize(vN);

  if (u_ViewMode == 2u) {
    oColor = vec4(N * 0.5 + 0.5, 1.0);
    oID = u_PickID;
    return;
  }

  vec3 V = normalize(-vPV);

  float viewDepth = -vPV.z;
  vec3 direct = computeDirectLightingForward(N, V, vP, viewDepth, baseColor,
                                             metallic, roughness);

  vec3 ambient = vec3(0.0);
  if (u_HasIBL != 0) {
    ambient = computeIBL(N, V, baseColor, metallic, roughness, ao,
                         u_EnvIrradiance, u_EnvPrefilter, u_BRDFLUT);
  } else {
    ambient = baseColor * ao * gSky.uSkyParams2.x;
  }

  vec3 hdr = direct + ambient;

  oColor = vec4(hdr, 1.0);
  oID = u_PickID;
}
