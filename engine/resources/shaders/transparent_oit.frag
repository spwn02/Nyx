#version 460 core

layout(location = 0) out vec4 oAccum;
layout(location = 1) out vec4 oReveal;

struct GpuMaterialPacked {
  vec4 baseColorFactor;
  vec4 emissiveFactor;
  vec4 mrAoFlags;
  uvec4 tex0123;
  uvec4 tex4_pad;
  vec4 uvScaleOffset;
  vec4 extra;
};

layout(std430, binding = 14) readonly buffer MaterialsSSBO {
  GpuMaterialPacked gMat[];
};

in VS_OUT {
  vec3 nrmW;
  vec3 posW;
  flat uint materialIndex;
} f;

float oitWeight(float a) {
  float w = clamp(a * 8.0 + 0.01, 0.01, 8.0);
  return w;
}

void main() {
  vec4 bc = gMat[f.materialIndex].baseColorFactor;
  float alpha = clamp(bc.a, 0.0, 1.0);
  if (alpha <= 0.001)
    discard;

  vec3 N = normalize(f.nrmW);
  vec3 L = normalize(vec3(0.6, 0.8, 0.2));
  float ndl = max(dot(N, L), 0.0);

  vec3 base = bc.rgb;
  vec3 emi = gMat[f.materialIndex].emissiveFactor.rgb;
  vec3 hdr = base * (0.12 + 1.15 * ndl) + emi;

  float w = oitWeight(alpha);
  oAccum = vec4(hdr * alpha * w, alpha * w);

  // Revealage target uses alpha in blending
  oReveal = vec4(alpha, 0.0, 0.0, 0.0);
}
