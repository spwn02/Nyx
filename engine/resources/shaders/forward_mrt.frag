#version 460 core

#ifdef GL_EXT_nonuniform_qualifier
#extension GL_EXT_nonuniform_qualifier : require
#define NONUNIFORM(x) nonuniformEXT(x)
#else
#define NONUNIFORM(x) (x)
#endif

layout(location = 0) out vec4 oHDR;
layout(location = 1) out uint oID;

#define NYX_MAX_TEX 16

#include "include/LightsCommon.glsl"
#include "include/common.glsl"
#include "include/SkyCommon.glsl"

struct GpuMaterialPacked {
  vec4 baseColorFactor; // rgba
  vec4 emissiveFactor;  // rgb + pad
  vec4 mrAoFlags;       // metallic, roughness, ao, flags
  uvec4 tex0123;        // base/emissive/normal/metallic
  uvec4 tex4_pad;       // roughness, ao, pad, pad
  vec4 uvScaleOffset;   // xy=scale, zw=offset
};

layout(std430, binding = 14) readonly buffer MaterialsSSBO {
  GpuMaterialPacked mats[];
} gMat;

layout(binding = 10) uniform sampler2D uTex2D[NYX_MAX_TEX];

layout(std430, binding = 15) readonly buffer MaterialTexRemap {
  uint remap[];
} gTexRemap;

uniform uint u_TexRemapCount;

layout(binding = 0) uniform samplerCube u_EnvIrradiance;
layout(binding = 1) uniform samplerCube u_EnvPrefilter;
layout(binding = 2) uniform sampler2D u_BRDFLUT;

layout(binding = 6) uniform sampler2D uShadowCSM;
layout(binding = 7) uniform sampler2D uShadowSpot;
layout(binding = 8) uniform sampler2D uShadowDir;
layout(binding = 9) uniform sampler2DArray uShadowPoint;

layout(std430, binding = 10) readonly buffer ShadowMetaSSBO {
  vec4 meta[];
} gShadowMeta;

layout(std140, binding = 5) uniform ShadowCSMData {
  mat4 uLightVP[4];
  vec4 uSplitDepths;
  vec4 uShadowMapSize; // xy = res, zw = inv res (tile)
  vec4 uBiasParams;    // x=normalBias, y=receiverBias, z=slopeBias
  vec4 uMisc;          // x=cascadeCount, y=camNear, z=camFar
  vec4 uLightDir;
  vec4 uAtlasUVMin[4];
  vec4 uAtlasUVMax[4];
};

uniform uint u_MaterialIndex;
uniform uint u_PickID;
uniform int u_HasIBL;
uniform mat4 u_View;
uniform int u_IsLight;
uniform vec4 u_LightColorIntensity;
uniform float u_LightExposure;

#include "include/forward_mrt_shadows.glsl"

in VS_OUT {
  vec3 posW;
  vec3 nrmW;
  vec2 uv;
  vec4 tanW;
} f;

const uint kInvalidTex = 0xFFFFFFFFu;

uint remapTex(uint tid) {
  if (tid == kInvalidTex || tid >= u_TexRemapCount)
    return kInvalidTex;
  return gTexRemap.remap[tid];
}

vec2 applyUV(vec2 uv, vec4 scaleOffset) {
  return uv * scaleOffset.xy + scaleOffset.zw;
}

vec3 sampleSRGB(uint tid, vec2 uv) {
  tid = remapTex(tid);
  if (tid == kInvalidTex || tid >= NYX_MAX_TEX)
    return vec3(1.0);
  return texture(uTex2D[NONUNIFORM(tid)], uv).rgb;
}

vec3 sampleLinear(uint tid, vec2 uv) {
  tid = remapTex(tid);
  if (tid == kInvalidTex || tid >= NYX_MAX_TEX)
    return vec3(1.0);
  return texture(uTex2D[NONUNIFORM(tid)], uv).rgb;
}

vec3 sampleNormalTS(uint tid, vec2 uv) {
  tid = remapTex(tid);
  if (tid == kInvalidTex || tid >= NYX_MAX_TEX)
    return vec3(0.0, 0.0, 1.0);
  vec3 n = texture(uTex2D[NONUNIFORM(tid)], uv).xyz * 2.0 - 1.0;
  return normalize(n);
}

bool hasTangent() {
  return length(f.tanW.xyz) > 0.0001 && abs(f.tanW.w) > 0.0001;
}

vec3 computeN(vec3 nrmW, vec4 tanW, uint normalTex, vec2 uv) {
  vec3 N = normalize(nrmW);
  if (normalTex == kInvalidTex)
    return N;
  if (!hasTangent())
    return N;
  vec3 T = normalize(tanW.xyz);
  vec3 B = normalize(cross(N, T) * tanW.w);
  mat3 TBN = mat3(T, B, N);
  vec3 nTS = sampleNormalTS(normalTex, uv);
  return normalize(TBN * nTS);
}

void main() {
  oID = u_PickID;

  if (u_IsLight != 0) {
    float intensity = u_LightColorIntensity.a;
    if (u_LightExposure != 0.0)
      intensity *= exp2(u_LightExposure);
    vec3 lightRgb = u_LightColorIntensity.rgb * intensity;
    oHDR = vec4(lightRgb, 1.0);
    return;
  }

  GpuMaterialPacked M = gMat.mats[u_MaterialIndex];

  vec2 uv = applyUV(f.uv, M.uvScaleOffset);

  vec3 baseFactor = M.baseColorFactor.rgb;
  vec3 emiFactor  = M.emissiveFactor.rgb;

  vec3 baseTex = sampleSRGB(M.tex0123.x, uv);
  vec3 emiTex  = sampleSRGB(M.tex0123.y, uv);

  vec3 metTex = sampleLinear(M.tex0123.w, uv);
  vec3 rghTex = sampleLinear(M.tex4_pad.x, uv);
  vec3 aoTex  = sampleLinear(M.tex4_pad.y, uv);
  float metallic  = clamp(M.mrAoFlags.x * metTex.r, 0.0, 1.0);
  float roughness = clamp(M.mrAoFlags.y * rghTex.r, 0.02, 1.0);
  float ao        = clamp(M.mrAoFlags.z * aoTex.r, 0.0, 1.0);

  vec3 base = baseFactor * baseTex;
  vec3 emi  = emiFactor  * emiTex;

  vec3 N = computeN(f.nrmW, f.tanW, M.tex0123.z, uv);
  vec3 V = normalize(- (u_View * vec4(f.posW, 1.0)).xyz);

  vec3 direct = vec3(0.0);
  uint lightCount = uLightCount;
  for (uint i = 0u; i < lightCount; ++i) {
    GpuLight Lg = gLights[i];
  uint type = uint(Lg.params.z + 0.5);
  // Defensive: if type is garbage, fall back based on radius/direction.
  if (type > LIGHT_SPOT) {
    type = (Lg.position.w > 0.0) ? LIGHT_POINT : LIGHT_DIR;
  }
    vec3 Ldir = vec3(0.0);
    float atten = 1.0;

    if (type == LIGHT_DIR) {
      if (length(Lg.direction.xyz) < 1e-6) {
        // If direction is missing, treat as point light to avoid NaNs.
        type = LIGHT_POINT;
      } else {
        Ldir = normalize(-Lg.direction.xyz);
      }
    } else {
      vec3 toL = Lg.position.xyz - f.posW;
      float dist = max(length(toL), 1e-4);
      Ldir = toL / dist;
      float radius = max(Lg.position.w, 0.001);
      float falloff = saturate(1.0 - dist / radius);
      atten = falloff * falloff;
    }

    if (type == LIGHT_SPOT) {
      float cosInner = Lg.params.x;
      float cosOuter = Lg.direction.w;
      float cosAng = dot(normalize(-Lg.direction.xyz), Ldir);
      float spot = saturate((cosAng - cosOuter) /
                            max(cosInner - cosOuter, 1e-4));
      atten *= spot;
    }

    float intensity = Lg.params.y;
    if (Lg.color.a != 0.0)
      intensity *= exp2(Lg.color.a);
    vec3 lightCol = Lg.color.rgb * intensity;
    float shadow = 1.0;

    if (Lg.params.w > 0.5) {
      uint metaIdx = uint(Lg.shadowData.x + 0.5);
      if (type == LIGHT_DIR) {
        if (Lg.shadowData.y > 0.5) {
          shadow = sampleShadowCSM(f.posW, N, Ldir);
        } else {
          shadow = sampleShadowDir(metaIdx, f.posW, N, Ldir);
        }
      } else if (type == LIGHT_SPOT) {
        shadow = sampleShadowSpot(metaIdx, f.posW, N, Ldir);
      } else if (type == LIGHT_POINT) {
        shadow = sampleShadowPoint(metaIdx, f.posW, N);
      }
    }

    direct += BRDF(N, V, Ldir, base, metallic, roughness) * lightCol *
              atten * shadow;
  }

  vec3 ambient = vec3(0.0);
  if (u_HasIBL != 0) {
    ambient = computeIBL(N, V, base, metallic, roughness, ao, u_EnvIrradiance,
                         u_EnvPrefilter, u_BRDFLUT);
  } else {
    ambient = base * ao * gSky.uSkyParams2.x;
  }

  vec3 color = direct + ambient + emi;

  oHDR = vec4(color, 1.0);
}
