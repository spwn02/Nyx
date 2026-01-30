#version 460

layout(location = 0) in vec3 aPos;

uniform mat4 u_ViewProj;
uniform mat4 u_Model;

out vec3 vWorldPos;

void main() {
  vec4 world = u_Model * vec4(aPos, 1.0);
  vWorldPos = world.xyz;
  gl_Position = u_ViewProj * world;
}
