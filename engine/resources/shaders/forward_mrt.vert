#version 460 core

layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aTan; // xyz tangent, w sign (0 = missing)
layout(location=3) in vec2 aUV;

uniform mat4 u_ViewProj;
uniform vec3 u_CamPos;

struct DrawData {
  mat4 model;
  uint materialIndex;
  uint pickID;
  uint meshHandle;
  uint _pad0;
};

layout(std430, binding = 13) readonly buffer PerDrawSSBO {
  DrawData gDraw[];
};

out VS_OUT {
  vec3 posW;
  vec3 nrmW;
  vec2 uv;
  vec2 uv0;
  vec4 tanW; // tangent in world + sign
  vec3 viewDirW;
  flat uint materialIndex;
  flat uint pickID;
} v;

void main() {
  DrawData d = gDraw[gl_BaseInstance];
  vec4 wp = d.model * vec4(aPos, 1.0);
  v.posW = wp.xyz;

  mat3 NrmM = mat3(transpose(inverse(d.model)));
  v.nrmW = normalize(NrmM * aNrm);

  // For rigid transforms you could use mat3(u_Model), but keep correct version.
  vec3 tW = (abs(aTan.w) < 1e-6) ? vec3(0.0)
                                : normalize(mat3(d.model) * aTan.xyz);
  v.tanW = vec4(tW, aTan.w);
  v.uv = aUV;
  v.uv0 = aUV;
  v.viewDirW = normalize(u_CamPos - v.posW);

  v.materialIndex = d.materialIndex;
  v.pickID = d.pickID;

  gl_Position = u_ViewProj * wp;
}
