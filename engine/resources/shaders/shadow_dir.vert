// Shadow depth vertex shader for directional lights (non-cascaded)
#version 460 core

layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;

uniform mat4 u_Model;
uniform mat4 u_ViewProj;

void main() {
  gl_Position = u_ViewProj * u_Model * vec4(aPosition, 1.0);
}
