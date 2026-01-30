#version 460

in vec3 vWorldPos;

uniform vec3 u_LightPos;
uniform float u_Far;

layout(location = 0) out float oDepth;

void main() {
  float d = length(vWorldPos - u_LightPos);
  oDepth = d / max(u_Far, 0.0001);
}
