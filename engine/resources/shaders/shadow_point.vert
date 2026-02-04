// Shadow depth vertex shader for point lights (cubemap)
#version 460 core

layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;

uniform mat4 u_Model;
uniform mat4 u_ViewProj;

out VS_OUT {
  vec3 fragPos;
} vs_out;

void main() {
  vec4 worldPos = u_Model * vec4(aPosition, 1.0);
  vs_out.fragPos = worldPos.xyz;
  gl_Position = u_ViewProj * worldPos;
}
