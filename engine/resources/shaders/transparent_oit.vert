#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;

uniform mat4 u_ViewProj;

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
  vec3 nrmW;
  vec3 posW;
  flat uint materialIndex;
} v;

void main() {
  DrawData d = gDraw[gl_BaseInstance];
  vec4 pw = d.model * vec4(aPos, 1.0);
  v.posW = pw.xyz;

  mat3 nrmM = mat3(transpose(inverse(d.model)));
  v.nrmW = normalize(nrmM * aNrm);
  v.materialIndex = d.materialIndex;

  gl_Position = u_ViewProj * pw;
}
