#version 460 core

layout(location = 0) out vec4 oColor;

#define NYX_MAX_TEX 16

#include "include/common.glsl"

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

uniform uint u_MaterialIndex;
uniform vec3 u_LightDir;    // direction to light
uniform vec3 u_LightColor;
uniform float u_LightIntensity;
uniform float u_LightExposure;
uniform float u_Ambient;
uniform vec3 u_CamPos;

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
  return texture(uTex2D[tid], uv).rgb;
}

vec3 sampleLinear(uint tid, vec2 uv) {
  tid = remapTex(tid);
  if (tid == kInvalidTex || tid >= NYX_MAX_TEX)
    return vec3(1.0);
  return texture(uTex2D[tid], uv).rgb;
}

vec3 sampleNormalTS(uint tid, vec2 uv) {
  tid = remapTex(tid);
  if (tid == kInvalidTex || tid >= NYX_MAX_TEX)
    return vec3(0.0, 0.0, 1.0);
  vec3 n = texture(uTex2D[tid], uv).xyz * 2.0 - 1.0;
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
  GpuMaterialPacked M = gMat.mats[u_MaterialIndex];
  vec2 uv = applyUV(f.uv, M.uvScaleOffset);

  vec3 baseTex = sampleSRGB(M.tex0123.x, uv);
  vec3 emiTex  = sampleSRGB(M.tex0123.y, uv);
  vec3 metTex  = sampleLinear(M.tex0123.w, uv);
  vec3 rghTex  = sampleLinear(M.tex4_pad.x, uv);
  vec3 aoTex   = sampleLinear(M.tex4_pad.y, uv);

  vec3 base = M.baseColorFactor.rgb * baseTex;
  vec3 emi  = M.emissiveFactor.rgb * emiTex;

  float metallic  = clamp(M.mrAoFlags.x * metTex.r, 0.0, 1.0);
  float roughness = clamp(M.mrAoFlags.y * rghTex.r, 0.02, 1.0);
  float ao        = clamp(M.mrAoFlags.z * aoTex.r, 0.0, 1.0);

  vec3 N = computeN(f.nrmW, f.tanW, M.tex0123.z, uv);
  vec3 V = normalize(u_CamPos - f.posW);
  vec3 L = normalize(u_LightDir);

  float intensity = u_LightIntensity;
  if (u_LightExposure != 0.0)
    intensity *= exp2(u_LightExposure);
  vec3 lightCol = u_LightColor * intensity;
  vec3 direct = BRDF(N, V, L, base, metallic, roughness) * lightCol;
  vec3 ambient = base * ao * u_Ambient;

  vec3 color = direct + ambient + emi;
  oColor = vec4(color, 1.0);
}
