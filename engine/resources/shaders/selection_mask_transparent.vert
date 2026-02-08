#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec4 aTan;
layout(location = 3) in vec2 aUV;

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
  flat uint pickID;
} v;

void main() {
  DrawData d = gDraw[gl_BaseInstance];
  gl_Position = u_ViewProj * (d.model * vec4(aPos, 1.0));
  v.pickID = d.pickID;
}
