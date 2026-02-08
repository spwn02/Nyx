#version 460 core

#ifdef GL_EXT_nonuniform_qualifier
#extension GL_EXT_nonuniform_qualifier : require
#define NONUNIFORM(x) nonuniformEXT(x)
#else
#define NONUNIFORM(x) (x)
#endif

layout(location = 0) out uint oID;

#define NYX_MAX_TEX 16
#define MAT_REMAP_TEX(x) remapTex(x)
#define MAT_UV0 (f.uv0 * gMat.mats[f.materialIndex].uvScaleOffset.xy + \
                 gMat.mats[f.materialIndex].uvScaleOffset.zw)

#include "include/common.glsl"

struct GpuMatGraphHeader {
  uint nodeOffset;
  uint nodeCount;
  uint outBaseColor;
  uint outMR;
  uint outNormalWS;
  uint outEmissive;
  uint outAlpha;
  uint alphaMode;
  float alphaCutoff;
  uint _pad0;
  uint _pad1;
};

struct GpuMatNode {
  uint op;
  uint dst;
  uint a;
  uint b;
  uint c;
  uint extra;
};

layout(std430, binding = 16) readonly buffer MatHeaders {
  GpuMatGraphHeader H[];
};
layout(std430, binding = 17) readonly buffer MatNodes {
  GpuMatNode N[];
};

struct GpuMaterialPacked {
  vec4 baseColorFactor; // rgba
  vec4 emissiveFactor;  // rgb + pad
  vec4 mrAoFlags;       // metallic, roughness, ao, flags
  uvec4 tex0123;        // base/emissive/normal/metallic
  uvec4 tex4_pad;       // roughness, ao, pad, pad
  vec4 uvScaleOffset;   // xy=scale, zw=offset
  vec4 extra;           // x=alphaCutoff, y=alphaMode, z/w=unused
};

layout(std430, binding = 14) readonly buffer MaterialsSSBO {
  GpuMaterialPacked mats[];
}
gMat;

layout(binding = 10) uniform sampler2D uTex2D[NYX_MAX_TEX];

layout(std430, binding = 15) readonly buffer MaterialTexRemap { uint remap[]; }
gTexRemap;

uniform uint u_TexRemapCount;

in VS_OUT {
  vec3 posW;
  vec3 nrmW;
  vec2 uv;
  vec2 uv0;
  vec4 tanW;
  vec3 viewDirW;
  flat uint materialIndex;
  flat uint pickID;
}
f;

const uint kInvalidTex = 0xFFFFFFFFu;

uint remapTex(uint tid) {
  if (tid == kInvalidTex || tid >= u_TexRemapCount)
    return kInvalidTex;
  return gTexRemap.remap[tid];
}

#define u_MaterialIndex f.materialIndex
#include "include/MaterialCommon.glsl"
#undef u_MaterialIndex

vec2 applyUV(vec2 uv, vec4 scaleOffset) {
  return uv * scaleOffset.xy + scaleOffset.zw;
}

float sampleAlpha(uint tid, vec2 uv) {
  tid = remapTex(tid);
  if (tid == kInvalidTex || tid >= NYX_MAX_TEX)
    return 1.0;
  return texture(uTex2D[NONUNIFORM(tid)], uv).a;
}

void main() {
  float alpha = 1.0;
  uint alphaMode = 0u;
  float alphaCutoff = 0.5;

  bool useGraph = false;
  if (f.materialIndex < H.length()) {
    useGraph = H[f.materialIndex].nodeCount > 0u;
  }

  if (useGraph) {
    vec3 base;
    float metallic;
    float roughness;
    float ao;
    vec3 emi;
    vec3 N;
    evalMaterial(base, metallic, roughness, ao, emi, N, alpha, alphaMode,
                 alphaCutoff);
  } else {
    GpuMaterialPacked M = gMat.mats[f.materialIndex];
    vec2 uv = applyUV(f.uv, M.uvScaleOffset);
    alpha = M.baseColorFactor.a * sampleAlpha(M.tex0123.x, uv);
    alphaMode = uint(M.extra.y + 0.5);
    alphaCutoff = M.extra.x;
  }

  if (alphaMode == Alpha_Mask) {
    if (alpha < alphaCutoff)
      discard;
  } else if (alphaMode == Alpha_Blend) {
    if (alpha < 0.01)
      discard;
  }

  oID = f.pickID;
}
